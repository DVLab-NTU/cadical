#include "internal.hpp"

#include "clause.hpp"
#include "macros.hpp"
#include "message.hpp"
#include "proof.hpp"

#include <algorithm>

namespace CaDiCaL {

void Internal::watch_clause (Clause * c) {
  const int size = c->size;
  const int l0 = c->literals[0];
  const int l1 = c->literals[1];
  watch_literal (l0, l1, c, size);
  watch_literal (l1, l0, c, size);
}

/*------------------------------------------------------------------------*/

void Internal::mark_removed (Clause * c, int except) {
  LOG (c, "marking removed");
  assert (!c->redundant);
  const const_literal_iterator end = c->end ();
  const_literal_iterator i;
  for (i = c->begin (); i != end; i++)
    if (*i != except) mark_removed (*i);
}

void Internal::mark_added (Clause * c) {
  LOG (c, "marking added");
  assert (likely_to_be_kept_clause (c));
  const const_literal_iterator end = c->end ();
  const_literal_iterator i;
  for (i = c->begin (); i != end; i++)
    mark_added (*i);
}

/*------------------------------------------------------------------------*/

// Redundant clauses of large glue and large size are extended to hold a
// 'analyzed' time stamp.  This makes memory allocation and deallocation a
// little bit tricky but saves space and time.  Since the embedding of the
// literals is really important and on the same level of complexity we keep
// both optimizations.

Clause * Internal::new_clause (bool red, int glue) {
  assert (clause.size () <= (size_t) INT_MAX);
  const int size = (int) clause.size ();  assert (size >= 2);
  bool have_pos = (size > 2), have_glue, have_analyzed;
  if (size == 2) have_pos = have_glue = have_analyzed = false;
  else if (red) {
    have_pos = have_glue = true;
    have_analyzed = (size > opts.keepsize && glue > opts.keepglue);
  } else have_pos = (size > 3), have_glue = have_analyzed = false;
#ifndef NDEBUG
  if (have_glue) assert (have_pos);
  if (have_analyzed) assert (have_glue);
#endif
  Clause * c;
  size_t offset = 0;
  if (!have_pos) offset += sizeof c->_pos;
  if (!have_glue) offset += sizeof c->_glue;
  if (!have_analyzed) offset += sizeof c->_analyzed;
  size_t bytes = sizeof (Clause) + (size - 2) * sizeof (int) - offset;
  char * ptr = new char[bytes];
  ptr -= offset;
  c = (Clause*) ptr;
  c->have.analyzed = have_analyzed;
  c->have.glue = have_glue;
  c->have.pos = have_pos;
  c->redundant = red;
  c->garbage = false;
  c->reason = false;
  c->moved = false;
  c->size = size;
  for (int i = 0; i < size; i++) c->literals[i] = clause[i];
  if (have_analyzed) c->_analyzed = ++stats.analyzed;
  if (have_glue) c->_glue = glue;
  if (have_pos) c->_pos = 2;
  assert (c->offset () == offset);
  if (red) stats.redundant++; else stats.irredundant++;
  clauses.push_back (c);
  LOG (c, "new");
  if (likely_to_be_kept_clause (c)) mark_added (c);
  return c;
}

// This is the 'raw' deallocation of a clause.  If the clause is in the
// arena nothing happens.  If the clause is not in the arena and its memory
// is reclaimed immediately and the allocation statistics is updated.

void Internal::deallocate_clause (Clause * c) {
  char * p = c->start ();
  if (arena.contains (p)) return;
  LOG (c, "deallocate");
  delete [] p;
}

void Internal::delete_clause (Clause * c) {
  LOG (c, "delete");
  size_t bytes = c->bytes ();
  stats.collected += bytes;
  if (c->garbage) assert (stats.garbage > 0), stats.garbage--;
  if (proof) proof->trace_delete_clause (c);
  deallocate_clause (c);
}

// We want to eagerly update statistics as soon clauses are marked garbage.
// Otherwise 'report' for instance gives wrong numbers after 'subsume'
// before the next 'reduce'.  Thus we factored out marking and accounting
// for garbage clauses.  Note that we do not update allocated bytes
// statistics at this point, but wait until the next 'collect'.  In order
// not to miss any update to those statistics we call 'check_clause_stats'
// after garbage collection in debugging mode.
//
void Internal::mark_garbage (Clause * c) {
  assert (!c->garbage);
  if (c->redundant) assert (stats.redundant), stats.redundant--;
  else assert (stats.irredundant), stats.irredundant--;
  stats.garbage++;
  c->garbage = true;
  if (!c->redundant) mark_removed (c);
}

bool Internal::tautological_clause () {
  sort (clause.begin (), clause.end (), lit_less_than ());
  const_int_iterator i = clause.begin ();
  int_iterator j = clause.begin ();
  int prev = 0;
  while (i != clause.end ()) {
    int lit = *i++;
    if (lit == -prev) return true;
    if (lit !=  prev) *j++ = prev = lit;
  }
  if (j != clause.end ()) {
    LOG ("removing %d duplicates", (long)(clause.end () - j));
    clause.resize (j - clause.begin ());
  }
  return false;
}

void Internal::add_new_original_clause () {
  int size = (int) clause.size ();
  if (!size) {
    if (!unsat) {
      MSG ("original empty clause");
      unsat = true;
    } else LOG ("original empty clause produces another inconsistency");
  } else if (size == 1) {
    int unit = clause[0], tmp = val (unit);
    if (!tmp) assign_unit (unit);
    else if (tmp < 0) {
      if (!unsat) {
        MSG ("parsed clashing unit");
        clashing = true;
      } else LOG ("original clashing unit produces another inconsistency");
    } else LOG ("original redundant unit");
  } else watch_clause (new_clause (false));
}

Clause * Internal::new_learned_redundant_clause (int glue) {
  Clause * res = new_clause (true, glue);
  if (proof) proof->trace_add_clause (res);
  watch_clause (res);
  return res;
}

Clause * Internal::new_resolved_irredundant_clause () {
  Clause * res = new_clause (false);
  if (proof) proof->trace_add_clause (res);
  assert (!watches ());
  return res;
}

};
