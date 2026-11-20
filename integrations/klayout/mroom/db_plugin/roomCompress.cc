#include "roomCompress.h"

#include "tlException.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace roomdb
{

namespace {

struct vector_cmp_x
{
  bool operator() (const db::Vector &a, const db::Vector &b) const
  {
    if (a.y () != b.y ()) {
      return a.y () < b.y ();
    }
    return a.x () < b.x ();
  }
};

struct vector_cmp_y
{
  bool operator() (const db::Vector &a, const db::Vector &b) const
  {
    if (a.x () != b.x ()) {
      return a.x () < b.x ();
    }
    return a.y () < b.y ();
  }
};

template <class PC>
struct rep_vector_cmp
{
  bool operator() (const std::pair<db::Vector, std::pair<db::Coord, int> > &a,
                   const std::pair<db::Vector, std::pair<db::Coord, int> > &b) const
  {
    if (a.second != b.second) {
      return a.second < b.second;
    }
    PC pc;
    return pc (a.first, b.first);
  }
};

template <class R>
R safe_diff (R a, R b)
{
  R d = a - b;
  if ((a > b && d < 0) || (a < b && d > 0)) {
    throw tl::Exception ("Signed coordinate difference overflow");
  }
  return d;
}

inline double cost_of (double d)
{
  int exp = 0;
  frexp (d, &exp);
  return double ((exp + 7) / 8);
}

} // namespace

void compress_displacements (std::vector<db::Vector> &points,
                             unsigned int level,
                             const std::function<void (const RegularRep &)> &on_regular,
                             const std::function<void (const IrregularRep &)> &on_irregular,
                             const std::function<void (const db::Vector &)> &on_single)
{
  typedef std::vector<db::Vector> disp_vector;
  typedef std::vector<std::pair<db::Vector, std::pair<db::Coord, int> > > tmp_rep_vector;

  if (points.empty ()) {
    return;
  }

  // OASIS: below threshold, irregular (or single).
  if (level < 1 || points.size () < 10) {
    std::sort (points.begin (), points.end (), vector_cmp_x ());
    if (points.size () == 1) {
      on_single (points.front ());
      points.clear ();
      return;
    }
    IrregularRep irr;
    irr.origin = points.front ();
    irr.offsets.reserve (points.size () - 1);
    for (std::size_t i = 1; i < points.size (); ++i) {
      irr.offsets.push_back (points[i] - irr.origin);
    }
    on_irregular (irr);
    points.clear ();
    return;
  }

  disp_vector displacements;
  tmp_rep_vector repetitions;
  std::vector<RegularRep> rep_vector;

  std::unordered_set<db::Coord> xcoords, ycoords;
  if (level > 1) {
    for (const db::Vector &d : points) {
      xcoords.insert (d.x ());
      ycoords.insert (d.y ());
    }
  }
  const bool xfirst = xcoords.size () < ycoords.size ();

  double simple_rep_cost = 0.0;
  double array_cost = 0.0;

  // OASIS tries both axes: each pass consumes current leftovers in `points`.
  for (int xypass = 0; xypass <= 1; ++xypass) {
    const bool xrep = (xfirst == (xypass == 0));

    displacements.clear ();
    repetitions.clear ();
    displacements.swap (points);

    if (xrep) {
      std::sort (displacements.begin (), displacements.end (), vector_cmp_x ());
    } else {
      std::sort (displacements.begin (), displacements.end (), vector_cmp_y ());
    }

    if (xypass == 0 && level > 1 && ! displacements.empty ()) {
      simple_rep_cost += cost_of (displacements.front ().x ()) + cost_of (displacements.front ().y ());
      for (disp_vector::iterator d = displacements.begin () + 1; d != displacements.end (); ++d) {
        simple_rep_cost += std::max (1.0, cost_of (double (d->x ()) - double (d[-1].x ())) +
                                              cost_of (double (d->y ()) - double (d[-1].y ())));
      }
    }

    disp_vector::iterator dwindow = displacements.begin ();
    for (disp_vector::iterator d = displacements.begin (); d != displacements.end (); ) {
      if (level < 2) {
        disp_vector::iterator dd = d;
        ++dd;
        db::Vector dxy;
        int nxy = 1;
        if (dd != displacements.end ()) {
          dxy = xrep ? db::Vector (safe_diff (dd->x (), d->x ()), 0)
                     : db::Vector (0, safe_diff (dd->y (), d->y ()));
          while (dd != displacements.end () && *dd == dd[-1] + dxy && nxy < std::numeric_limits<int>::max ()) {
            ++dd;
            ++nxy;
          }
        }
        if (nxy < 3) {
          points.push_back (*d++);
        } else {
          repetitions.push_back (std::make_pair (*d, std::make_pair (xrep ? dxy.x () : dxy.y (), nxy)));
          d = dd;
        }
      } else {
        int nxy_max = 1;
        unsigned int nn_max = 0;
        if (d == dwindow) {
          for (dwindow = d + 1;
               dwindow != displacements.end () && (xrep ? (dwindow->y () == d->y ()) : (dwindow->x () == d->x ()));
               ++dwindow) {
          }
        }
        for (unsigned int nn = 0; nn < level; ++nn) {
          disp_vector::iterator dd = d + (nn + 1);
          if (dd >= dwindow) {
            break;
          }
          db::Vector dxy = xrep ? db::Vector (safe_diff (dd->x (), d->x ()), 0)
                                : db::Vector (0, safe_diff (dd->y (), d->y ()));
          int nxy = 2;
          while (dd != dwindow && nxy < std::numeric_limits<int>::max ()) {
            disp_vector::iterator df = std::lower_bound (dd + 1, dwindow, *dd + dxy);
            if (df == dwindow || *df != *dd + dxy) {
              break;
            }
            ++nxy;
            dd = df;
          }
          if (nxy > nxy_max) {
            nxy_max = nxy;
            nn_max = nn;
          }
        }
        if (nxy_max < 2) {
          points.push_back (*d++);
        } else {
          db::Vector dxy_max = xrep ? db::Vector (safe_diff ((d + nn_max + 1)->x (), d->x ()), 0)
                                    : db::Vector (0, safe_diff ((d + nn_max + 1)->y (), d->y ()));
          disp_vector::iterator ds = dwindow;
          disp_vector::iterator dt = dwindow;
          db::Vector df = *d + dxy_max * long (nxy_max - 1);
          do {
            --ds;
            if (*ds != df) {
              *--dt = *ds;
            } else {
              df -= dxy_max;
            }
          } while (ds != d);
          repetitions.push_back (
            std::make_pair (*d, std::make_pair (xrep ? dxy_max.x () : dxy_max.y (), nxy_max)));
          d = dt;
        }
      }
    }

    // Compact 1D runs into 2D lattices (y then x).
    for (int xypass2 = 1; xypass2 >= 0; --xypass2) {
      if (xypass2) {
        std::sort (repetitions.begin (), repetitions.end (), rep_vector_cmp<vector_cmp_y> ());
      } else {
        std::sort (repetitions.begin (), repetitions.end (), rep_vector_cmp<vector_cmp_x> ());
      }

      tmp_rep_vector::iterator rw = repetitions.begin ();
      for (tmp_rep_vector::const_iterator r = repetitions.begin (); r != repetitions.end (); ) {
        tmp_rep_vector::const_iterator rr = r;
        ++rr;
        db::Vector dxy2;
        if (rr != repetitions.end ()) {
          dxy2 = xypass2 ? db::Vector (0, safe_diff (rr->first.y (), r->first.y ()))
                         : db::Vector (safe_diff (rr->first.x (), r->first.x ()), 0);
        }
        int nxy2 = 1;
        db::Vector dxy2n (dxy2);
        while (rr != repetitions.end () && rr->second == r->second && rr->first == r->first + dxy2n) {
          ++nxy2;
          ++rr;
          dxy2n += dxy2;
        }
        if (nxy2 < 2 && xypass2) {
          *rw++ = *r;
        } else {
          RegularRep reg;
          reg.origin = r->first;
          reg.a = db::Vector (xrep ? r->second.first : 0, xrep ? 0 : r->second.first);
          reg.b = dxy2;
          reg.na = static_cast<unsigned long> (r->second.second);
          reg.nb = static_cast<unsigned long> (nxy2);
          if (level < 2) {
            on_regular (reg);
          } else {
            rep_vector.push_back (reg);
          }
        }
        r = rr;
      }
      repetitions.erase (rw, repetitions.end ());
    }
  }

  if (level > 1) {
    if (! points.empty ()) {
      array_cost += cost_of (points.front ().x ()) + cost_of (points.front ().y ());
      for (disp_vector::const_iterator d = points.begin () + 1; d != points.end (); ++d) {
        array_cost += std::max (1.0, cost_of (d->x () - d[-1].x ()) + cost_of (d->y () - d[-1].y ()));
      }
    }
    bool array_set = false;
    db::Vector a_ref, b_ref;
    size_t in_ref = 0, im_ref = 0;
    bool ref_set = false;
    db::Coord x_ref = 0, y_ref = 0;
    for (const RegularRep &r : rep_vector) {
      array_cost += 2;
      if (! ref_set || x_ref != r.origin.x ()) {
        array_cost += cost_of (r.origin.x ());
      }
      if (! ref_set || y_ref != r.origin.y ()) {
        array_cost += cost_of (r.origin.y ());
      }
      ref_set = true;
      x_ref = r.origin.x ();
      y_ref = r.origin.y ();
      if (! array_set || r.a != a_ref || r.b != b_ref || r.na != in_ref || r.nb != im_ref) {
        array_set = true;
        a_ref = r.a;
        b_ref = r.b;
        in_ref = r.na;
        im_ref = r.nb;
        array_cost += cost_of (r.a.x ()) + cost_of (r.b.x ()) + cost_of (r.a.y ()) + cost_of (r.b.y ()) +
                      cost_of (r.na) + cost_of (r.nb);
      } else {
        array_cost += 1;
      }
    }
    if (array_cost > simple_rep_cost) {
      for (const RegularRep &r : rep_vector) {
        for (unsigned long i = 0; i < r.na; ++i) {
          for (unsigned long j = 0; j < r.nb; ++j) {
            points.push_back (r.origin + r.a * long (i) + r.b * long (j));
          }
        }
      }
      rep_vector.clear ();
      std::sort (points.begin (), points.end (), vector_cmp_x ());
    }
  }

  for (const RegularRep &r : rep_vector) {
    on_regular (r);
  }

  if (points.size () > 1) {
    std::sort (points.begin (), points.end (), vector_cmp_x ());
    IrregularRep irr;
    irr.origin = points.front ();
    irr.offsets.reserve (points.size () - 1);
    for (std::size_t i = 1; i < points.size (); ++i) {
      irr.offsets.push_back (points[i] - irr.origin);
    }
    on_irregular (irr);
  } else if (! points.empty ()) {
    on_single (points.front ());
  }
  points.clear ();
}

} // namespace roomdb
