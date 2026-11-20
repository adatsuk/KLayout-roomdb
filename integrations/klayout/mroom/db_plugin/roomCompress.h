#ifndef HDR_roomCompress
#define HDR_roomCompress

#include "dbTypes.h"
#include "dbPoint.h"
#include "dbTrans.h"

#include <functional>
#include <vector>

namespace roomdb
{

/*! OASIS-writer-compatible repetition extracted from a displacement cloud. */
struct RegularRep
{
  db::Vector origin;
  db::Vector a;
  db::Vector b;
  unsigned long na = 1;
  unsigned long nb = 1;
};

struct IrregularRep
{
  db::Vector origin;
  std::vector<db::Vector> offsets; // relative to origin, first (0,0) omitted
};

/*!
 * Compress a list of absolute displacements the way OASISWriter::Compressor does
 * at the given compression level (default OASIS level is 2).
 *
 * The input vector is consumed. Callbacks receive regular lattices, irregular
 * leftovers, and single placements.
 */
void compress_displacements (std::vector<db::Vector> &displacements,
                             unsigned int level,
                             const std::function<void (const RegularRep &)> &on_regular,
                             const std::function<void (const IrregularRep &)> &on_irregular,
                             const std::function<void (const db::Vector &)> &on_single);

} // namespace roomdb

#endif
