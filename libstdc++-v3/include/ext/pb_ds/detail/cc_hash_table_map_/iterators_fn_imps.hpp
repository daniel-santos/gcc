// -*- C++ -*-

// Copyright (C) 2005, 2006, 2009, 2011 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 3, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file cc_hash_table_map_/iterators_fn_imps.hpp
 * Contains implementations of cc_ht_map_'s iterators related functions, e.g.,
 * begin().
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::s_end_it;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::s_const_end_it;

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
begin()
{
  pointer p_value;
  std::pair<entry_pointer, size_type> pos;
  get_start_it_state(p_value, pos);
  return iterator(p_value, pos, this);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::iterator
PB_DS_CLASS_C_DEC::
end()
{ return s_end_it; }

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::
begin() const
{
  pointer p_value;
  std::pair<entry_pointer, size_type> pos;
  get_start_it_state(p_value, pos);
  return const_iterator(p_value, pos, this);
}

PB_DS_CLASS_T_DEC
inline typename PB_DS_CLASS_C_DEC::const_iterator
PB_DS_CLASS_C_DEC::
end() const
{ return s_const_end_it; }

