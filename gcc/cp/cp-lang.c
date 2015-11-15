/* Language-dependent hooks for C++.
   Copyright (C) 2001-2015 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva  <aoliva@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "stor-layout.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "cp-objcp-common.h"

enum c_language_kind c_language = clk_cxx;
static void cp_init_ts (void);
static const char * cxx_dwarf_name (tree t, int verbosity);
static enum classify_record cp_classify_record (tree type);
static tree cp_eh_personality (void);
static tree get_template_innermost_arguments_folded (const_tree);
static tree get_template_argument_pack_elems_folded (const_tree);
static tree cxx_enum_underlying_base_type (const_tree);

/* Lang hooks common to C++ and ObjC++ are declared in cp/cp-objcp-common.h;
   consequently, there should be very few hooks below.  */

#undef LANG_HOOKS_NAME
#define LANG_HOOKS_NAME "GNU C++"
#undef LANG_HOOKS_INIT
#define LANG_HOOKS_INIT cxx_init
#undef LANG_HOOKS_CLASSIFY_RECORD
#define LANG_HOOKS_CLASSIFY_RECORD cp_classify_record
#undef LANG_HOOKS_GENERIC_TYPE_P
#define LANG_HOOKS_GENERIC_TYPE_P class_tmpl_impl_spec_p

#undef LANG_HOOKS_GET_INNERMOST_GENERIC_PARMS
#define LANG_HOOKS_GET_INNERMOST_GENERIC_PARMS \
	get_primary_template_innermost_parameters
#undef LANG_HOOKS_GET_INNERMOST_GENERIC_ARGS
#define LANG_HOOKS_GET_INNERMOST_GENERIC_ARGS \
	get_template_innermost_arguments_folded
#undef LANG_HOOKS_FUNCTION_PARAMETER_PACK_P
#define LANG_HOOKS_FUNCTION_PARAMETER_PACK_P \
	function_parameter_pack_p
#undef LANG_HOOKS_GET_ARGUMENT_PACK_ELEMS
#define LANG_HOOKS_GET_ARGUMENT_PACK_ELEMS \
	get_template_argument_pack_elems_folded
#undef LANG_HOOKS_GENERIC_GENERIC_PARAMETER_DECL_P
#define LANG_HOOKS_GENERIC_GENERIC_PARAMETER_DECL_P \
	template_template_parameter_p
#undef LANG_HOOKS_FUNCTION_PARM_EXPANDED_FROM_PACK_P
#define LANG_HOOKS_FUNCTION_PARM_EXPANDED_FROM_PACK_P \
	function_parameter_expanded_from_pack_p
#undef LANG_HOOKS_GET_GENERIC_FUNCTION_DECL
#define LANG_HOOKS_GET_GENERIC_FUNCTION_DECL get_function_template_decl
#undef LANG_HOOKS_DWARF_NAME
#define LANG_HOOKS_DWARF_NAME cxx_dwarf_name
#undef LANG_HOOKS_INIT_TS
#define LANG_HOOKS_INIT_TS cp_init_ts
#undef LANG_HOOKS_EH_PERSONALITY
#define LANG_HOOKS_EH_PERSONALITY cp_eh_personality
#undef LANG_HOOKS_EH_RUNTIME_TYPE
#define LANG_HOOKS_EH_RUNTIME_TYPE build_eh_type_type
#undef LANG_HOOKS_ENUM_UNDERLYING_BASE_TYPE
#define LANG_HOOKS_ENUM_UNDERLYING_BASE_TYPE cxx_enum_underlying_base_type
#undef LANG_HOOKS_EMPTY_RECORD_P
#define LANG_HOOKS_EMPTY_RECORD_P is_really_empty_class

/* Each front end provides its own lang hook initializer.  */
struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

/* Lang hook routines common to C++ and ObjC++ appear in cp/cp-objcp-common.c;
   there should be very few routines below.  */

/* The following function does something real, but only in Objective-C++.  */

tree
objcp_tsubst_copy_and_build (tree /*t*/,
			     tree /*args*/,
			     tsubst_flags_t /*complain*/,
			     tree /*in_decl*/,
			     bool /*function_p*/)
{
  return NULL_TREE;
}

static void
cp_init_ts (void)
{
  cp_common_init_ts ();

  init_shadowed_var_for_decl ();
}

static const char *
cxx_dwarf_name (tree t, int verbosity)
{
  gcc_assert (DECL_P (t));

  if (DECL_NAME (t)
      && (anon_aggrname_p (DECL_NAME (t)) || LAMBDA_TYPE_P (t)))
    return NULL;
  if (verbosity >= 2)
    return decl_as_dwarf_string (t,
                                 TFF_DECL_SPECIFIERS | TFF_UNQUALIFIED_NAME
                                 | TFF_NO_OMIT_DEFAULT_TEMPLATE_ARGUMENTS);

  return lang_decl_dwarf_name (t, verbosity, false);
}

static enum classify_record
cp_classify_record (tree type)
{
  if (TYPE_LANG_SPECIFIC (type)
      && CLASSTYPE_DECLARED_CLASS (type))
    return RECORD_IS_CLASS;

  return RECORD_IS_STRUCT;
}

static GTY(()) tree cp_eh_personality_decl;

static tree
cp_eh_personality (void)
{
  if (!cp_eh_personality_decl)
    {
      const char *lang = (pragma_java_exceptions ? "gcj" : "gxx");
      cp_eh_personality_decl = build_personality_function (lang);
    }

  return cp_eh_personality_decl;
}

/* This is a subroutine of fold_cplus_constants.  It returns TRUE if T
   is a C++ specific constant that needs to be folded further before
   being passed to the debug info emitter.  */

static bool
template_arg_needs_folding (const_tree t)
{
  /* For now only PTRMEM_CST nodes are to be folded further.  */
  if (TREE_CODE (t) == PTRMEM_CST)
    return true;
  return false;
}

/* Fold the elements of the TREE_VEC C which are C++ specific nodes
   that would need folding so that they can be processed by the debug
   info emitter. This is a subroutine of
   get_template_innermost_arguments_folded and
   get_template_argument_pack_elems_folded.  */

static tree
fold_cplus_constants (const_tree c)
{
  tree folded_elems, elems = CONST_CAST_TREE (c);
  int vec_len, i;

  if (elems == NULL_TREE || elems == error_mark_node)
    return elems;

  vec_len = TREE_VEC_LENGTH (elems);

  /* First check if there is at least one element that needs
     folding. If there is none, we just return ELEMS. Otherwise create
     and return a new tree vector that contains the folded versions of
     ELEMS. This is to avoid allocating memory if we don't need
     to.  */
  for (i = 0; i < vec_len; ++i)
    {
      if (template_arg_needs_folding (TREE_VEC_ELT (elems, i)))
	break;
    }
  if (i == vec_len)
    return elems;

  folded_elems = make_tree_vec (vec_len);
  for (i = 0; i < vec_len; ++i)
    {
      tree elem = TREE_VEC_ELT (elems, i);
      TREE_VEC_ELT (folded_elems, i) =  
	(elem && !TYPE_P (elem)) ? cplus_expand_constant (elem) : elem;

    }
  return folded_elems;
}

/* The C++ implementation of the LANG_HOOKS_GET_INNERMOST_GENERIC_ARGS
   hook. It returns the innermost template arguments of type T, and
   makes sure those arguments are folded enough for the debug info
   emitter.  */

static tree
get_template_innermost_arguments_folded (const_tree t)
{
  return fold_cplus_constants (get_template_innermost_arguments (t));
}

static tree
get_template_argument_pack_elems_folded (const_tree t)
{
  return fold_cplus_constants (get_template_argument_pack_elems (t));
}

/* The C++ version of the enum_underlying_base_type langhook.
   See also cp/semantics.c (finish_underlying_type).  */

static
tree cxx_enum_underlying_base_type (const_tree type)
{
  tree underlying_type = ENUM_UNDERLYING_TYPE (type);

  if (! ENUM_FIXED_UNDERLYING_TYPE_P (type))
    underlying_type
      = c_common_type_for_mode (TYPE_MODE (underlying_type),
                                TYPE_UNSIGNED (underlying_type));

  return underlying_type;
}

#include "gt-cp-cp-lang.h"
#include "gtype-cp.h"
