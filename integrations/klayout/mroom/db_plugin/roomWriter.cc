#include "roomWriter.h"
#include "propertyBridge.h"

#include "block.h"
#include "cell.h"
#include "cell_content.h"
#include "database.h"
#include "instance.h"
#include "layer_utils.h"
#include "room_file_io.h"
#include "serialization.h"
#include "shape.h"
#include "types.h"

#include "lstrCompressed.h"

#include "dbBox.h"
#include "dbCell.h"
#include "dbInstances.h"
#include "dbLayout.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbShape.h"
#include "dbText.h"
#include "dbTrans.h"

#include "tlException.h"
#include "tlFileUtils.h"
#include "tlInternational.h"
#include "tlStream.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace roomdb
{

namespace {

constexpr bool k_oasis_recompress = false;

std::int64_t cast_coord (db::Coord value)
{
  return static_cast<std::int64_t> (value);
}

room::Point make_point (const db::Point &point)
{
  return room::Point { cast_coord (point.x ()), cast_coord (point.y ()) };
}

room::Point make_point (const db::Vector &vector)
{
  return room::Point { cast_coord (vector.x ()), cast_coord (vector.y ()) };
}

room::Orient orient_from_fp (int code)
{
  switch (code) {
  case db::FTrans::r0: return room::Orient::R0;
  case db::FTrans::r90: return room::Orient::R90;
  case db::FTrans::r180: return room::Orient::R180;
  case db::FTrans::r270: return room::Orient::R270;
  case db::FTrans::m0: return room::Orient::MY;
  case db::FTrans::m45: return room::Orient::MY90;
  case db::FTrans::m90: return room::Orient::MX;
  case db::FTrans::m135: return room::Orient::MX90;
  default: return room::Orient::R0;
  }
}

room::Transform make_transform (const db::ICplxTrans &trans)
{
  room::Transform result;
  const db::Vector disp = trans.disp ();
  result.x = cast_coord (disp.x ());
  result.y = cast_coord (disp.y ());
  result.mag = trans.mag ();
  result.orient = orient_from_fp (trans.fp_trans ().rot ());
  return result;
}

room::LayerSpec layer_spec_from_properties (const db::LayerProperties &properties)
{
  room::LayerSpec spec;
  if (properties.layer >= 0) {
    spec.layerNum = static_cast<std::uint16_t> (properties.layer);
  }
  if (properties.datatype >= 0) {
    spec.dataType = static_cast<std::uint16_t> (properties.datatype);
  }
  if (! properties.name.empty ()) {
    spec.name = properties.name;
  }
  return spec;
}

class TlKjOutputStream : public kj::OutputStream
{
public:
  explicit TlKjOutputStream (tl::OutputStream &stream) : m_stream (stream) {}

  void write (const void *buffer, size_t size) override
  {
    if (size == 0) {
      return;
    }
    m_stream.put_raw (reinterpret_cast<const char *> (buffer), size);
  }

private:
  tl::OutputStream &m_stream;
};

bool env_flag (const char *name, bool default_value)
{
  const char *raw = std::getenv (name);
  if (raw == nullptr || *raw == '\0') {
    return default_value;
  }
  if (raw[0] == '0' && raw[1] == '\0') {
    return false;
  }
  if ((raw[0] == 'n' || raw[0] == 'N') && (raw[1] == '\0' || raw[1] == 'o' || raw[1] == 'O')) {
    return false;
  }
  if ((raw[0] == 'f' || raw[0] == 'F') && (raw[1] == '\0' || raw[1] == 'a' || raw[1] == 'A')) {
    return false;
  }
  return true;
}

int env_int (const char *name, int default_value)
{
  const char *raw = std::getenv (name);
  if (raw == nullptr || *raw == '\0') {
    return default_value;
  }
  char *end = nullptr;
  const long value = std::strtol (raw, &end, 10);
  if (end == raw) {
    return default_value;
  }
  return static_cast<int> (value);
}

void write_database_to_stream (room::Database &database, tl::OutputStream &stream,
                                double &capnp_s, double &zlib_s)
{
  capnp_s = 0.0;
  zlib_s = 0.0;
  const auto capnp_started = std::chrono::steady_clock::now ();
  database.lib ().recomputeAllBBoxes ();
  database.lib ().refreshIndex ();
  database.setFileSummary (room::FileSummary::fromLib (database.lib (), room::ViewType::Layout));

  // LStream already attached columns/rows/copies — skip a second compact optimize pass by default.
  // Tunables (read each write):
  //   ROOM_COMPRESS=0|1          whole-file ROOMZ zlib (default 1)
  //   ROOM_COMPRESS_LEVEL=0..9   zlib level (default 1 = Z_BEST_SPEED)
  //   ROOM_DETECT_REPETITION=0|1 second compact optimize (default 0)
  //   ROOM_REP_LEVEL=0|2         LStream array search (default 2). 0 keeps arrays already
  //                              present and does not search for new shape arrays.
  //   ROOM_WRITE_THREADS=N       cells searched in parallel (default: half the logical processors)
  room::SaveOptions options;
  options.compactGeometry = true;
  options.detectRepetition = env_flag ("ROOM_DETECT_REPETITION", false);
  options.compressFile = env_flag ("ROOM_COMPRESS", true);
  options.compressLevel = env_int ("ROOM_COMPRESS_LEVEL", 1);

  capnp::MallocMessageBuilder message;
  writeDatabase (message.initRoot<room::schema::Database> (), database, options);
  capnp_s = std::chrono::duration<double> (std::chrono::steady_clock::now () - capnp_started).count ();

  if (options.compressFile) {
    const auto zlib_started = std::chrono::steady_clock::now ();
    const std::vector<std::uint8_t> framed =
        room::compressRoomMessage (message, options.compressLevel);
    stream.put_raw (reinterpret_cast<const char *> (framed.data ()), framed.size ());
    zlib_s = std::chrono::duration<double> (std::chrono::steady_clock::now () - zlib_started).count ();
    return;
  }

  // Uncompressed path: stream Cap'n Proto segments straight to KLayout's OutputStream
  // (no intermediate full-file VectorOutputStream copy).
  TlKjOutputStream adapter (stream);
  capnp::writeMessage (adapter, message);
}

void report_write_timing (const char *line)
{
  std::fputs (line, stderr);
  std::fflush (stderr);
  const char *path = std::getenv ("ROOM_TIMING_LOG");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::FILE *out = std::fopen (path, "a");
  if (out == nullptr) {
    return;
  }
  std::fputs (line, out);
  std::fclose (out);
}

double seconds_since (std::chrono::steady_clock::time_point started)
{
  return std::chrono::duration<double> (std::chrono::steady_clock::now () - started).count ();
}

struct LayerJob
{
  unsigned int layer_index = 0;
  db::LayerProperties properties;
};

struct CellJob
{
  db::cell_index_type cell_index = 0;
  const char *cell_name = nullptr;
  db::properties_id_type prop_id = 0;
  std::vector<LayerJob> layers;
};

struct LayerOut
{
  db::LayerProperties properties;
  lstr::Compressed compressed;
};

struct CellOut
{
  double search_s = 0.0;
  std::vector<LayerOut> layers;
  lstr::Compressed instances;
};

CellOut compress_cell_job (const db::Layout &layout, const CellJob &job,
                           const std::set<db::cell_index_type> &cells_to_write,
                           unsigned int rep_level)
{
  CellOut out;
  const db::Cell &cell = layout.cell (job.cell_index);
  out.layers.reserve (job.layers.size ());
  for (const LayerJob &layer : job.layers) {
    LayerOut layer_out;
    layer_out.properties = layer.properties;
    const auto started = std::chrono::steady_clock::now ();
    layer_out.compressed.compress_shapes (cell.shapes (layer.layer_index), rep_level, k_oasis_recompress);
    out.search_s += seconds_since (started);
    out.layers.push_back (std::move (layer_out));
  }

  const auto started = std::chrono::steady_clock::now ();
  out.instances.compress_instances (cell.begin (), cells_to_write, rep_level);
  out.search_s += seconds_since (started);
  return out;
}

void emit_compressed_shapes (room::Block &block, std::uint32_t layer_id, lstr::Compressed &compressed);
void emit_compressed_instances (room::Block &block, const db::Layout &layout, lstr::Compressed &compressed);

void emit_compressed_cell (room::Database &database, const db::Layout &layout, double dbu_per_micron,
                           const CellJob &job, CellOut &cell_out, double &block_s)
{
  room::Cell &core_cell = database.lib ().getOrCreateCell (job.cell_name);
  core_cell.properties () = properties_from_klayout (job.prop_id);
  room::CellContent &content = core_cell.getOrCreateContent (room::ViewType::Layout, dbu_per_micron);
  content.setDbuPerMicron (dbu_per_micron);
  content.layers ().clear ();

  auto layer_id_for = [&content, &database] (const db::LayerProperties &properties) -> std::uint32_t {
    return findOrAddViewLayer (content, layer_spec_from_properties (properties), database.lib ().layers ());
  };

  for (LayerOut &layer_out : cell_out.layers) {
    const std::uint32_t layer_id = layer_id_for (layer_out.properties);
    const auto started = std::chrono::steady_clock::now ();
    emit_compressed_shapes (content.block (), layer_id, layer_out.compressed);
    block_s += seconds_since (started);
  }

  {
    const auto started = std::chrono::steady_clock::now ();
    emit_compressed_instances (content.block (), layout, cell_out.instances);
    block_s += seconds_since (started);
  }

  {
    const auto started = std::chrono::steady_clock::now ();
    content.block ().recomputeBBox ();
    block_s += seconds_since (started);
  }
}

struct WorkItem
{
  std::size_t cell = 0;
  int layer = -1;
};

struct CellSlot
{
  CellOut out;
  int remaining = 0;
  double search_s = 0.0;
  double wall0 = -1.0;
  double wall1 = 0.0;
};

struct ParallelCompress
{
  const db::Layout *layout = nullptr;
  const std::vector<CellJob> *jobs = nullptr;
  const std::vector<WorkItem> *tasks = nullptr;
  std::vector<CellSlot> *cellBins = nullptr;
  const std::set<db::cell_index_type> *cells_to_write = nullptr;
  unsigned int rep_level = 2;
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::unique_ptr<CellOut>> done;
  std::vector<double> finished_at;
  std::size_t next_task = 0;
  std::size_t next_emit = 0;
  double max_piece_s = 0.0;
  bool stop = false;
  std::exception_ptr error;
  std::chrono::steady_clock::time_point started;
};

void compress_worker (ParallelCompress &state)
{
  try {
    for (;;) {
      WorkItem item;
      {
        std::unique_lock<std::mutex> lock (state.mu);
        state.cv.wait (lock, [&] {
          return state.stop || state.error || state.next_task < state.tasks->size ();
        });
        if (state.stop || state.error || state.next_task >= state.tasks->size ()) {
          return;
        }
        item = (*state.tasks)[state.next_task++];
      }

      const CellJob &job = (*state.jobs)[item.cell];
      const db::Cell &cell = state.layout->cell (job.cell_index);
      const double began = seconds_since (state.started);
      LayerOut layer_out;
      lstr::Compressed instances;
      if (item.layer < 0) {
        instances.compress_instances (cell.begin (), *state.cells_to_write, state.rep_level);
      } else {
        const LayerJob &layer = job.layers[static_cast<std::size_t> (item.layer)];
        layer_out.properties = layer.properties;
        layer_out.compressed.compress_shapes (cell.shapes (layer.layer_index), state.rep_level, k_oasis_recompress);
      }
      const double ended = seconds_since (state.started);
      const double piece_s = ended - began;

      {
        std::lock_guard<std::mutex> lock (state.mu);
        CellSlot &slot = (*state.cellBins)[item.cell];
        if (item.layer < 0) {
          slot.out.instances = std::move (instances);
        } else {
          slot.out.layers[static_cast<std::size_t> (item.layer)] = std::move (layer_out);
        }
        slot.search_s += piece_s;
        if (slot.wall0 < 0.0 || began < slot.wall0) {
          slot.wall0 = began;
        }
        if (ended > slot.wall1) {
          slot.wall1 = ended;
        }
        if (piece_s > state.max_piece_s) {
          state.max_piece_s = piece_s;
        }
        slot.remaining -= 1;
        if (slot.remaining == 0) {
          slot.out.search_s = slot.search_s;
          state.done[item.cell] = std::make_unique<CellOut> (std::move (slot.out));
          state.finished_at[item.cell] = ended;
        }
      }
      state.cv.notify_all ();
    }
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock (state.mu);
      if (! state.error) {
        state.error = std::current_exception ();
      }
      state.stop = true;
    }
    state.cv.notify_all ();
  }
}

struct ThreadJoin
{
  ParallelCompress &state;
  std::vector<std::thread> &threads;

  ~ThreadJoin ()
  {
    {
      std::lock_guard<std::mutex> lock (state.mu);
      state.stop = true;
    }
    state.cv.notify_all ();
    for (std::thread &thread : threads) {
      if (thread.joinable ()) {
        thread.join ();
      }
    }
  }
};

unsigned int write_thread_count (std::size_t jobs)
{
  if (jobs == 0) {
    return 1;
  }
  const int requested = env_int ("ROOM_WRITE_THREADS", -1);
  unsigned int count = 1;
  if (requested < 0) {
    // Hyperthreads made the array search slower on this host (8-way 22.2 s, 4-way 19.9 s).
    const unsigned int hardware = std::thread::hardware_concurrency ();
    count = hardware <= 1 ? 1 : hardware / 2;
  } else if (requested > 1) {
    count = static_cast<unsigned int> (requested);
  }
  if (count > jobs) {
    count = static_cast<unsigned int> (jobs);
  }
  return count == 0 ? 1 : count;
}

std::string resolve_lib_name (const db::Layout &layout, const db::SaveLayoutOptions &options, const tl::OutputStream &stream)
{
  std::string lib_name = options.libname ();
  if (lib_name.empty () && layout.has_meta_info ("libname")) {
    const tl::Variant libname = layout.meta_info ("libname").value;
    if (libname.is_a_string ()) {
      lib_name = libname.to_stdstring ();
    }
  }
  if (lib_name.empty ()) {
    const std::string path = stream.path ();
    if (! path.empty ()) {
      lib_name = tl::basename (path);
    }
  }
  if (lib_name.empty ()) {
    lib_name = "default";
  }
  return lib_name;
}

struct RepMaps
{
  std::unordered_map<std::uint64_t, lstr::RegularArray> regular;
  std::unordered_map<std::uint64_t, const std::vector<db::Vector> *> irregular;
};

RepMaps build_rep_maps (const lstr::Compressed &compressed)
{
  RepMaps maps;
  for (auto it = compressed.begin_regular_arrays (); it != compressed.end_regular_arrays (); ++it) {
    maps.regular.emplace (it->second, it->first);
  }
  for (auto it = compressed.begin_irregular_arrays (); it != compressed.end_irregular_arrays (); ++it) {
    maps.irregular.emplace (it->second, &it->first);
  }
  return maps;
}

template <class Data>
void apply_repetition (Data &data, std::uint64_t rep_id, const RepMaps &maps)
{
  if (rep_id == 0) {
    return;
  }

  const auto regular_it = maps.regular.find (rep_id);
  if (regular_it != maps.regular.end ()) {
    const lstr::RegularArray &array = regular_it->second;
    if (! array.is_null () && (array.na () > 1 || array.nb () > 1)) {
      data.columns = static_cast<std::uint32_t> (array.na ());
      data.rows = static_cast<std::uint32_t> (array.nb ());
      data.stepX = cast_coord (array.a ().x ());
      data.colY = cast_coord (array.a ().y ());
      data.rowX = cast_coord (array.b ().x ());
      data.stepY = cast_coord (array.b ().y ());
    }
    return;
  }

  const auto irregular_it = maps.irregular.find (rep_id);
  if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr && ! irregular_it->second->empty ()) {
    // lstr irregular offsets omit (0,0); ROOM copies include the origin like OASIS.
    data.copies.clear ();
    data.copies.reserve (irregular_it->second->size () + 1);
    data.copies.push_back (room::Point { 0, 0 });
    for (const db::Vector &offset : *irregular_it->second) {
      data.copies.push_back (make_point (offset));
    }
  }
}

template <class Poly>
std::vector<room::Point> polygon_points (const Poly &poly)
{
  std::vector<room::Point> points;
  for (auto p = poly.begin_hull (); p != poly.end_hull (); ++p) {
    points.push_back (make_point (*p));
  }
  return points;
}

std::vector<room::Point> path_points (const db::Path &path)
{
  std::vector<room::Point> points;
  for (auto p = path.begin (); p != path.end (); ++p) {
    points.push_back (make_point (*p));
  }
  return points;
}

void emit_box (room::Block &block, std::uint32_t layer_id, const db::Box &box,
               db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  if (box.empty ()) {
    return;
  }
  room::Shape::RectData rect;
  rect.layerId = layer_id;
  rect.box = room::Box {
    cast_coord (box.left ()), cast_coord (box.bottom ()),
    cast_coord (box.right ()), cast_coord (box.top ())
  };
  apply_repetition (rect, rep_id, maps);
  block.shapes ().emplace_back (rect);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

bool polygon_as_box (const std::vector<room::Point> &points, room::Box &box)
{
  if (points.size () < 4) {
    return false;
  }
  std::int64_t llx = points[0].x;
  std::int64_t lly = points[0].y;
  std::int64_t urx = points[0].x;
  std::int64_t ury = points[0].y;
  for (const room::Point &point : points) {
    llx = std::min (llx, point.x);
    lly = std::min (lly, point.y);
    urx = std::max (urx, point.x);
    ury = std::max (ury, point.y);
  }
  if (urx <= llx || ury <= lly) {
    return false;
  }
  unsigned corners = 0;
  unsigned axis = 0;
  for (const room::Point &point : points) {
    const bool on_x = (point.x == llx || point.x == urx);
    const bool on_y = (point.y == lly || point.y == ury);
    if (on_x && on_y) {
      ++corners;
    } else if (on_x || on_y) {
      ++axis;
    } else {
      return false;
    }
  }
  if (corners < 4) {
    return false;
  }
  (void) axis;
  box = room::Box { llx, lly, urx, ury };
  return true;
}

void emit_polygon (room::Block &block, std::uint32_t layer_id, std::vector<room::Point> points,
                   db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  if (points.size () < 3) {
    return;
  }

  room::Box box;
  if (polygon_as_box (points, box)) {
    room::Shape::RectData rect;
    rect.layerId = layer_id;
    rect.box = box;
    apply_repetition (rect, rep_id, maps);
    block.shapes ().emplace_back (rect);
    if (prop_id != 0) {
      block.shapes ().back ().properties () = properties_from_klayout (prop_id);
    }
    return;
  }

  room::Shape::PolygonData polygon;
  polygon.layerId = layer_id;
  polygon.points = std::move (points);
  apply_repetition (polygon, rep_id, maps);
  block.shapes ().emplace_back (polygon);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_path (room::Block &block, std::uint32_t layer_id, const db::Path &path,
                db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  std::vector<room::Point> points = path_points (path);
  if (points.empty ()) {
    return;
  }
  room::Shape::PathData path_data;
  path_data.layerId = layer_id;
  path_data.points = std::move (points);
  path_data.width = static_cast<std::uint32_t> (std::max<db::Coord> (0, path.width ()));
  path_data.bgnExt = static_cast<std::int32_t> (path.bgn_ext ());
  path_data.endExt = static_cast<std::int32_t> (path.end_ext ());
  path_data.round = path.round ();
  apply_repetition (path_data, rep_id, maps);
  block.shapes ().emplace_back (path_data);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_text (room::Block &block, std::uint32_t layer_id, const db::Text &text,
                db::properties_id_type prop_id)
{
  room::Shape::TextData label;
  label.layerId = layer_id;
  label.text = text.string ();
  const db::Vector disp = text.trans ().disp ();
  label.position = make_point (disp);
  label.height = static_cast<std::uint32_t> (std::max<db::Coord> (0, text.size ()));
  block.shapes ().emplace_back (label);
  if (prop_id != 0) {
    block.shapes ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_text_with_rep (room::Block &block, std::uint32_t layer_id, const db::Text &text,
                         db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  // ROOM text records have no repetition fields; expand like a flat label list.
  if (rep_id == 0) {
    emit_text (block, layer_id, text, prop_id);
    return;
  }

  const auto regular_it = maps.regular.find (rep_id);
  if (regular_it != maps.regular.end () && ! regular_it->second.is_null ()) {
    const lstr::RegularArray &array = regular_it->second;
    for (size_t ia = 0; ia < array.na (); ++ia) {
      for (size_t ib = 0; ib < array.nb (); ++ib) {
        db::Text copy (text);
        copy.transform (db::Trans (array.a () * long (ia) + array.b () * long (ib)));
        emit_text (block, layer_id, copy, prop_id);
      }
    }
    return;
  }

  const auto irregular_it = maps.irregular.find (rep_id);
  if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr) {
    emit_text (block, layer_id, text, prop_id);
    for (const db::Vector &offset : *irregular_it->second) {
      db::Text copy (text);
      copy.transform (db::Trans (offset));
      emit_text (block, layer_id, copy, prop_id);
    }
    return;
  }

  emit_text (block, layer_id, text, prop_id);
}

template <class Obj, class EmitPlain>
void emit_container (const lstr::Compressed::compressed_container<Obj> &container,
                     EmitPlain &&emit_plain)
{
  for (const Obj &obj : container.plain) {
    emit_plain (obj, 0, 0);
  }
  for (const auto &entry : container.with_properties) {
    emit_plain (entry.first, entry.second, 0);
  }
  for (const auto &entry : container.array) {
    emit_plain (entry.first, 0, entry.second);
  }
  for (const auto &entry : container.array_with_properties) {
    emit_plain (entry.first.first, entry.first.second, entry.second);
  }
}

void emit_compressed_shapes (room::Block &block, std::uint32_t layer_id, lstr::Compressed &compressed)
{
  const RepMaps maps = build_rep_maps (compressed);

  emit_container (compressed.get_container<db::Box> (),
                  [&] (const db::Box &box, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_box (block, layer_id, box, prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Polygon> (),
                  [&] (const db::Polygon &poly, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_polygon (block, layer_id, polygon_points (poly), prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::SimplePolygon> (),
                  [&] (const db::SimplePolygon &poly, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_polygon (block, layer_id, polygon_points (poly), prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Path> (),
                  [&] (const db::Path &path, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_path (block, layer_id, path, prop_id, rep_id, maps);
                  });

  emit_container (compressed.get_container<db::Text> (),
                  [&] (const db::Text &text, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_text_with_rep (block, layer_id, text, prop_id, rep_id, maps);
                  });
}

void emit_instance (room::Block &block, const db::Layout &layout, const db::CellInstArray &arr,
                    db::properties_id_type prop_id, std::uint64_t rep_id, const RepMaps &maps)
{
  // compress_instances always delivers size-1 CellInstArray objects; repetition is in rep_id.
  const char *child_name = layout.cell_name (arr.object ().cell_index ());
  if (child_name == nullptr || *child_name == '\0') {
    return;
  }

  room::Instance instance (child_name, make_transform (arr.complex_trans ()));

  if (rep_id != 0) {
    const auto regular_it = maps.regular.find (rep_id);
    if (regular_it != maps.regular.end () && ! regular_it->second.is_null ()
        && (regular_it->second.na () > 1 || regular_it->second.nb () > 1)) {
      const lstr::RegularArray &array = regular_it->second;
      instance.setArray (static_cast<std::uint32_t> (array.na ()), static_cast<std::uint32_t> (array.nb ()),
                         cast_coord (array.a ().x ()), cast_coord (array.b ().y ()),
                         cast_coord (array.a ().y ()), cast_coord (array.b ().x ()));
    } else {
      const auto irregular_it = maps.irregular.find (rep_id);
      if (irregular_it != maps.irregular.end () && irregular_it->second != nullptr
          && ! irregular_it->second->empty ()) {
        std::vector<room::Point> copies;
        copies.reserve (irregular_it->second->size () + 1);
        copies.push_back (room::Point { 0, 0 });
        for (const db::Vector &offset : *irregular_it->second) {
          copies.push_back (make_point (offset));
        }
        instance.setOffsets (std::move (copies));
      }
    }
  }

  block.instances ().emplace_back (std::move (instance));
  if (prop_id != 0) {
    block.instances ().back ().properties () = properties_from_klayout (prop_id);
  }
}

void emit_shapes_keeping_arrays (room::Block &block, std::uint32_t layer_id, const db::Shapes &shapes)
{
  // Same transfer as LStream's in-array branch, without the neighbor search on loose shapes.
  lstr::Compressed repetition;
  for (db::ShapeIterator shape = shapes.begin (db::ShapeIterator::All); ! shape.at_end (); ) {
    lstr::RegularArray regular;
    std::vector<db::Vector> irregular;
    const bool in_array = shape.in_array ();
    if (in_array) {
      repetition.create_repetition (shape.array (), regular, irregular);
    }

    RepMaps maps;
    std::uint64_t rep_id = 0;
    if (! regular.is_null () && (regular.na () > 1 || regular.nb () > 1)) {
      maps.regular.emplace (1, regular);
      rep_id = 1;
    } else if (! irregular.empty ()) {
      maps.irregular.emplace (1, &irregular);
      rep_id = 1;
    }

    const db::properties_id_type prop_id = shape->prop_id ();
    if (shape->is_box ()) {
      db::Box box;
      shape->instantiate (box);
      emit_box (block, layer_id, box, prop_id, rep_id, maps);
    } else if (shape->is_simple_polygon ()) {
      db::SimplePolygon polygon;
      shape->instantiate (polygon);
      emit_polygon (block, layer_id, polygon_points (polygon), prop_id, rep_id, maps);
    } else if (shape->is_polygon ()) {
      db::Polygon polygon;
      shape->instantiate (polygon);
      emit_polygon (block, layer_id, polygon_points (polygon), prop_id, rep_id, maps);
    } else if (shape->is_path ()) {
      db::Path path;
      shape->instantiate (path);
      emit_path (block, layer_id, path, prop_id, rep_id, maps);
    } else if (shape->is_text ()) {
      db::Text text;
      shape->instantiate (text);
      emit_text_with_rep (block, layer_id, text, prop_id, rep_id, maps);
    }

    if (in_array) {
      shape.finish_array ();
    } else {
      ++shape;
    }
  }
}

void emit_compressed_instances (room::Block &block, const db::Layout &layout, lstr::Compressed &compressed)
{
  const RepMaps maps = build_rep_maps (compressed);
  emit_container (compressed.get_container<db::CellInstArray> (),
                  [&] (const db::CellInstArray &arr, db::properties_id_type prop_id, std::uint64_t rep_id) {
                    emit_instance (block, layout, arr, prop_id, rep_id, maps);
                  });
}

} // namespace

void Writer::write (db::Layout &layout, tl::OutputStream &stream, const db::SaveLayoutOptions &options)
{
  const std::string lib_name = resolve_lib_name (layout, options, stream);
  layout.add_meta_info ("libname", db::MetaInfo (tl::to_string (tr ("Library name")), lib_name));

  room::Database database;
  database.setGenerator ("KLayout ROOM writer");
  database.lib () = room::Lib (lib_name);
  database.lib ().properties () = properties_from_klayout (layout.prop_id ());

  const double layout_dbu = layout.dbu ();
  const double dbu_per_micron = (layout_dbu > 0.0) ? (1.0 / layout_dbu) : 1000.0;

  std::map<std::pair<std::uint16_t, std::uint16_t>, room::LayerSpec> lib_layer_catalog;
  for (unsigned int li = 0; li < layout.layers (); ++li) {
    if (! layout.is_valid_layer (li)) {
      continue;
    }
    const db::LayerProperties &properties = layout.get_properties (li);
    if (properties.layer < 0 || properties.datatype < 0) {
      continue;
    }
    const auto key = std::make_pair (static_cast<std::uint16_t> (properties.layer),
                                     static_cast<std::uint16_t> (properties.datatype));
    if (lib_layer_catalog.find (key) == lib_layer_catalog.end ()) {
      lib_layer_catalog[key] = layer_spec_from_properties (properties);
    }
  }
  for (const auto &entry : lib_layer_catalog) {
    database.lib ().layers ().push_back (entry.second);
  }

  std::set<db::cell_index_type> cells_to_write;
  for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
    cells_to_write.insert (cit->cell_index ());
  }

  const int rep_level = env_int ("ROOM_REP_LEVEL", 2);
  double search_cpu_s = 0.0;
  double search_wall_s = 0.0;
  double block_s = 0.0;
  double max_cell_s = 0.0;
  double max_piece_s = 0.0;
  unsigned int threads_used = 1;

  if (rep_level <= 0) {
    // No shape-array search. Keep already stored arrays and emit on this thread.
    for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
      const db::Cell &cell = *cit;
      const db::cell_index_type cell_index = cell.cell_index ();
      const char *cell_name = layout.cell_name (cell_index);
      if (cell_name == nullptr || *cell_name == '\0') {
        continue;
      }

      room::Cell &core_cell = database.lib ().getOrCreateCell (cell_name);
      core_cell.properties () = properties_from_klayout (cell.prop_id ());
      room::CellContent &content = core_cell.getOrCreateContent (room::ViewType::Layout, dbu_per_micron);
      content.setDbuPerMicron (dbu_per_micron);
      content.layers ().clear ();

      auto layer_id_for = [&content, &database] (const db::LayerProperties &properties) -> std::uint32_t {
        return findOrAddViewLayer (content, layer_spec_from_properties (properties), database.lib ().layers ());
      };

      for (unsigned int li = 0; li < layout.layers (); ++li) {
        if (! layout.is_valid_layer (li)) {
          continue;
        }
        const db::LayerProperties &properties = layout.get_properties (li);
        if (properties.layer < 0 || properties.datatype < 0) {
          continue;
        }
        if (cell.shapes (li).empty ()) {
          continue;
        }
        const std::uint32_t layer_id = layer_id_for (properties);
        const auto started = std::chrono::steady_clock::now ();
        emit_shapes_keeping_arrays (content.block (), layer_id, cell.shapes (li));
        block_s += seconds_since (started);
      }

      {
        // LStream level 0 drops instance arrays whose size is greater than 1.
        lstr::Compressed compressed;
        const auto started = std::chrono::steady_clock::now ();
        compressed.compress_instances (cell.begin (), cells_to_write, 2u);
        search_cpu_s += seconds_since (started);
        const auto emit_started = std::chrono::steady_clock::now ();
        emit_compressed_instances (content.block (), layout, compressed);
        block_s += seconds_since (emit_started);
      }

      const auto bbox_started = std::chrono::steady_clock::now ();
      content.block ().recomputeBBox ();
      block_s += seconds_since (bbox_started);
    }
    search_wall_s = search_cpu_s;
    max_cell_s = search_cpu_s;
  } else {
    std::vector<CellJob> jobs;
    for (db::Layout::const_iterator cit = layout.begin (); cit != layout.end (); ++cit) {
      const db::Cell &cell = *cit;
      const char *cell_name = layout.cell_name (cell.cell_index ());
      if (cell_name == nullptr || *cell_name == '\0') {
        continue;
      }
      CellJob job;
      job.cell_index = cell.cell_index ();
      job.cell_name = cell_name;
      job.prop_id = cell.prop_id ();
      for (unsigned int li = 0; li < layout.layers (); ++li) {
        if (! layout.is_valid_layer (li)) {
          continue;
        }
        const db::LayerProperties &properties = layout.get_properties (li);
        if (properties.layer < 0 || properties.datatype < 0 || cell.shapes (li).empty ()) {
          continue;
        }
        LayerJob layer;
        layer.layer_index = li;
        layer.properties = properties;
        job.layers.push_back (layer);
      }
      jobs.push_back (std::move (job));
    }

    threads_used = write_thread_count (jobs.size ());
    std::vector<CellSlot> cellBins (jobs.size ());
    std::vector<WorkItem> tasks;
    for (std::size_t i = 0; i < jobs.size (); ++i) {
      cellBins[i].out.layers.resize (jobs[i].layers.size ());
      cellBins[i].remaining = static_cast<int> (jobs[i].layers.size ()) + 1;
      for (std::size_t layer = 0; layer < jobs[i].layers.size (); ++layer) {
        tasks.push_back (WorkItem { i, static_cast<int> (layer) });
      }
      tasks.push_back (WorkItem { i, -1 });
    }
    ParallelCompress state;
    state.layout = &layout;
    state.jobs = &jobs;
    state.tasks = &tasks;
    state.cellBins = &cellBins;
    state.cells_to_write = &cells_to_write;
    state.rep_level = static_cast<unsigned int> (rep_level);
    state.done.resize (jobs.size ());
    state.finished_at.assign (jobs.size (), 0.0);
    state.started = std::chrono::steady_clock::now ();

    {
      std::vector<std::thread> threads;
      threads.reserve (threads_used);
      ThreadJoin join { state, threads };
      for (unsigned int t = 0; t < threads_used; ++t) {
        threads.emplace_back (compress_worker, std::ref (state));
      }

      for (std::size_t i = 0; i < jobs.size (); ++i) {
        std::unique_ptr<CellOut> out;
        double finished_at = 0.0;
        {
          std::unique_lock<std::mutex> lock (state.mu);
          state.cv.wait (lock, [&] { return state.error || state.done[i] != nullptr; });
          if (state.error) {
            break;
          }
          out = std::move (state.done[i]);
          finished_at = state.finished_at[i];
          state.next_emit = i + 1;
        }
        state.cv.notify_all ();

        search_cpu_s += out->search_s;
        const double cell_wall = cellBins[i].wall1 - cellBins[i].wall0;
        if (cell_wall > max_cell_s) {
          max_cell_s = cell_wall;
        }
        if (finished_at > search_wall_s) {
          search_wall_s = finished_at;
        }
        emit_compressed_cell (database, layout, dbu_per_micron, jobs[i], *out, block_s);
      }
    }
    max_piece_s = state.max_piece_s;
    if (state.error) {
      std::rethrow_exception (state.error);
    }
  }

  double capnp_s = 0.0;
  double zlib_s = 0.0;
  write_database_to_stream (database, stream, capnp_s, zlib_s);
  char timing_line[320];
  std::snprintf (timing_line, sizeof (timing_line),
                 "ROOM_WRITE_TIMING threads %u search_cpu_s %.3f search_wall_s %.3f max_cell_s %.3f max_piece_s %.3f block_s %.3f capnp_s %.3f zlib_s %.3f\n",
                 threads_used, search_cpu_s, search_wall_s, max_cell_s, max_piece_s, block_s, capnp_s, zlib_s);
  report_write_timing (timing_line);
}

} // namespace roomdb
