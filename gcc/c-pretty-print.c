/* Subroutines common to both C and C++ pretty-printers.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "real.h"
#include "c-pretty-print.h"

/* literal  */
static void pp_c_char              PARAMS ((c_pretty_print_info *, int));
static void pp_c_character_literal PARAMS ((c_pretty_print_info *, tree));
static void pp_c_bool_literal      PARAMS ((c_pretty_print_info *, tree));
static bool pp_c_enumerator        PARAMS ((c_pretty_print_info *, tree));
static void pp_c_integer_literal   PARAMS ((c_pretty_print_info *, tree));
static void pp_c_real_literal      PARAMS ((c_pretty_print_info *, tree));
static void pp_c_string_literal    PARAMS ((c_pretty_print_info *, tree));

static void pp_c_primary_expression PARAMS ((c_pretty_print_info *, tree));

/* postfix-expression  */
static void pp_c_initializer_list PARAMS ((c_pretty_print_info *, tree));

static void pp_c_unary_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_multiplicative_expression PARAMS ((c_pretty_print_info *,
						    tree));
static void pp_c_additive_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_shift_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_relational_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_equality_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_and_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_exclusive_or_expression PARAMS ((c_pretty_print_info *,
						  tree));
static void pp_c_inclusive_or_expression PARAMS ((c_pretty_print_info *,
						  tree));
static void pp_c_logical_and_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_conditional_expression PARAMS ((c_pretty_print_info *, tree));
static void pp_c_assignment_expression PARAMS ((c_pretty_print_info *, tree));

/* Declarations.  */

/* Print out CV-qualifiers.  Take care of possible extension.  */
void
pp_c_cv_qualifier (ppi, cv)
     c_pretty_print_info *ppi;
     int cv;
{
  if (cv & TYPE_QUAL_CONST)
    pp_c_identifier (ppi, "const");
  if (cv & TYPE_QUAL_VOLATILE)
    pp_c_identifier (ppi, "volatile");
  if (cv & TYPE_QUAL_RESTRICT)
    pp_c_identifier (ppi, flag_isoc99 ? "restrict" : "__restrict__");
}


/* Statements.  */


/* Expressions.  */

/* Print out a c-char.  */
static void
pp_c_char (ppi, c)
     c_pretty_print_info *ppi;
     int c;
{
  switch (c)
    {
    case TARGET_NEWLINE:
      pp_identifier (ppi, "\\n");
      break;
    case TARGET_TAB:
      pp_identifier (ppi, "\\t");
      break;
    case TARGET_VT:
      pp_identifier (ppi, "\\v");
      break;
    case TARGET_BS:
      pp_identifier (ppi, "\\b");
      break;
    case TARGET_CR:
      pp_identifier (ppi, "\\r");
      break;
    case TARGET_FF:
      pp_identifier (ppi, "\\f");
      break;
    case TARGET_BELL:
      pp_identifier (ppi, "\\a");
      break;
    case '\\':
      pp_identifier (ppi, "\\\\");
      break;
    case '\'':
      pp_identifier (ppi, "\\'");
      break;
    case '\"':
      pp_identifier (ppi, "\\\"");
      break;
    default:
      if (ISPRINT (c))
	pp_character (ppi, c);
      else
	pp_format_integer (ppi, "\\%03o", (unsigned) c);
      break;
    }
}

/* Print out a STRING literal.  */
static inline void
pp_c_string_literal (ppi, s)
     c_pretty_print_info *ppi;
     tree s;
{
  const char *p = TREE_STRING_POINTER (s);
  int n = TREE_STRING_LENGTH (s) - 1;
  int i;
  pp_doublequote (ppi);
  for (i = 0; i < n; ++i)
    pp_c_char (ppi, p[i]);
  pp_doublequote (ppi);
}

/* Print out a CHARACTER literal.  */
static inline void
pp_c_character_literal (ppi, c)
     c_pretty_print_info *ppi;
     tree c;
{
  pp_quote (ppi);
  pp_c_char (ppi, tree_low_cst (c, 0));
  pp_quote (ppi);
}

/* Print out a BOOLEAN literal.  */
static inline void
pp_c_bool_literal (ppi, b)
     c_pretty_print_info *ppi;
     tree b;
{
  if (b == boolean_false_node || integer_zerop (b))
    {
      if (c_language == clk_cplusplus)
	pp_c_identifier (ppi, "false");
      else if (c_language == clk_c && flag_isoc99)
	pp_c_identifier (ppi, "_False");
      else
	pp_unsupported_tree (ppi, b);
    }
  else if (b == boolean_true_node)
    {
      if (c_language == clk_cplusplus)
	pp_c_identifier (ppi, "true");
      else if (c_language == clk_c && flag_isoc99)
	pp_c_identifier (ppi, "_True");
      else
	pp_unsupported_tree (ppi, b);
    }
  else
    pp_unsupported_tree (ppi, b);
}

/* Attempt to print out an ENUMERATOR.  Return true on success.  Else return 
   false; that means the value was obtained by a cast, in which case
   print out the type-id part of the cast-expression -- the casted value
   is then printed by pp_c_integer_literal.  */
static bool
pp_c_enumerator (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  tree type = TREE_TYPE (e);
  tree value;

  /* Find the name of this constant.  */
  for (value = TYPE_VALUES (type); 
       value != NULL_TREE && !tree_int_cst_equal (TREE_VALUE (value), e);
       value = TREE_CHAIN (value))
    ;
  
  if (value != NULL_TREE)
    pp_c_tree_identifier (ppi, TREE_PURPOSE (value));
  else
    {
      /* Value must have been cast.  */
      pp_c_left_paren (ppi);
      pp_type_id (ppi, type);
      pp_c_right_paren (ppi);
      return false;
    }
  
  return true;
}

/* Print out an INTEGER constant value.  */
static void
pp_c_integer_literal (ppi, i)
     c_pretty_print_info *ppi;
     tree i;
{
  tree type = TREE_TYPE (i);
  
  if (type == boolean_type_node)
    pp_c_bool_literal (ppi, i);
  else if (type == char_type_node)
    pp_c_character_literal (ppi, i);
  else if (TREE_CODE (type) == ENUMERAL_TYPE
	   && pp_c_enumerator (ppi, i))
    ;
  else
    {
      if (host_integerp (i, 0))
	pp_wide_integer (ppi, TREE_INT_CST_LOW (i));
      else
	{
	  if (tree_int_cst_sgn (i) < 0)
	    {
	      static char format[10]; /* "%x%09999x\0" */
	      if (!format[0])
		sprintf (format, "%%x%%0%dx", HOST_BITS_PER_INT / 4);

	      pp_c_char (ppi, '-');
	      i = build_int_2 (-TREE_INT_CST_LOW (i),
			       ~TREE_INT_CST_HIGH (i) + !TREE_INT_CST_LOW (i));
	      sprintf (pp_buffer (ppi)->digit_buffer, format, 
		       TREE_INT_CST_HIGH (i), TREE_INT_CST_LOW (i));
	      pp_identifier (ppi, pp_buffer (ppi)->digit_buffer);

	    }
	}
    }
}

/* Print out a REAL value. */
static inline void
pp_c_real_literal (ppi, r)
     c_pretty_print_info *ppi;
     tree r;
{
  REAL_VALUE_TO_DECIMAL (TREE_REAL_CST (r), "%.16g",
			 pp_buffer (ppi)->digit_buffer);
  pp_identifier (ppi, pp_buffer(ppi)->digit_buffer);
}


void
pp_c_literal (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  switch (TREE_CODE (e))
    {
    case INTEGER_CST:
      pp_c_integer_literal (ppi, e);
      break;
      
    case REAL_CST:
      pp_c_real_literal (ppi, e);
      break;
      
    case STRING_CST:
      pp_c_string_literal (ppi, e);
      break;      

    default:
      pp_unsupported_tree (ppi, e);
      break;
    }
}

/* Pretty-print a C primary-expression.  */
static void
pp_c_primary_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  switch (TREE_CODE (e))
    {
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case CONST_DECL:
    case FUNCTION_DECL:
    case LABEL_DECL:
      e = DECL_NAME (e);
      /* Fall through.  */
    case IDENTIFIER_NODE:
      pp_c_tree_identifier (ppi, e);
      break;

    case ERROR_MARK:
      pp_c_identifier (ppi, "<erroneous-expression>");
      break;
		       
    case RESULT_DECL:
      pp_c_identifier (ppi, "<return-value>");
      break;

    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      pp_c_literal (ppi, e);
      break;

    case TARGET_EXPR:
      pp_c_left_paren (ppi);
      pp_c_identifier (ppi, "__builtin_memcpy");
      pp_c_left_paren (ppi);
      pp_ampersand (ppi);
      pp_c_primary_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_ampersand (ppi);
      pp_initializer (ppi, TREE_OPERAND (e, 1));
      if (TREE_OPERAND (e, 2))
	{
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_OPERAND (e, 2));
	}
      pp_c_right_paren (ppi);

    case STMT_EXPR:
      pp_c_left_paren (ppi);
      pp_statement (ppi, STMT_EXPR_STMT (e));
      pp_c_right_paren (ppi);
      break;

    default:
      /*  Make sure this call won't cause any infinite loop. */
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, e);
      pp_c_right_paren (ppi);
      break;
    }
}

/* Print out a C initializer -- also support C compound-literals.  */
void
pp_c_initializer (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == CONSTRUCTOR)
    {
      enum tree_code code = TREE_CODE (TREE_TYPE (e));
      if (code == RECORD_TYPE || code == UNION_TYPE || code == ARRAY_TYPE)
	{
	  pp_left_brace (ppi);
	  pp_c_initializer_list (ppi, e);
	  pp_right_brace (ppi);
	}
      else
	pp_unsupported_tree (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_assignment_expression (ppi, e);
}

static void
pp_c_initializer_list (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  tree type = TREE_TYPE (e);
  const enum tree_code code = TREE_CODE (type);

  if (code == RECORD_TYPE || code == UNION_TYPE || code == ARRAY_TYPE)
    {
      tree init = TREE_OPERAND (e, 1);
      for (; init != NULL_TREE; init = TREE_CHAIN (init))
	{
	  if (code == RECORD_TYPE || code == UNION_TYPE)
	    {
	      pp_dot (ppi);
	      pp_c_primary_expression (ppi, TREE_PURPOSE (init));
	    }
	  else
	    {
	      pp_c_left_bracket (ppi);
	      if (TREE_PURPOSE (init))
		pp_c_literal (ppi, TREE_PURPOSE (init));
	      pp_c_right_bracket (ppi);
	    }
	  pp_c_whitespace (ppi);
	  pp_equal (ppi);
	  pp_c_whitespace (ppi);
	  pp_initializer (ppi, TREE_VALUE (init));
	  if (TREE_CHAIN (init))
	    pp_separate_with (ppi, ',');
	}
    }
  else
    pp_unsupported_tree (ppi, type);
}

void
pp_c_postfix_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_identifier (ppi, code == POSTINCREMENT_EXPR ? "++" : "--");
      break;
      
    case ARROW_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_arrow (ppi);
      break;

    case ARRAY_REF:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_left_bracket (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_right_bracket (ppi);
      break;

    case CALL_EXPR:
      pp_postfix_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_left_paren (ppi);
      pp_c_expression_list (ppi, TREE_OPERAND (e, 1));
      pp_c_right_paren (ppi);
      break;

    case ABS_EXPR:
    case FFS_EXPR:
      pp_c_identifier (ppi, 
		       code == ABS_EXPR ? "__builtin_abs" : "__builtin_ffs");
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_right_paren (ppi);
      break;

    case COMPONENT_REF:
      {
	tree object = TREE_OPERAND (e, 0);
	if (TREE_CODE (object) == INDIRECT_REF)
	  {
	    pp_postfix_expression (ppi, TREE_OPERAND (object, 0));
	    pp_arrow (ppi);
	  }
	else
	  {
	    pp_postfix_expression (ppi, object);
	    pp_dot (ppi);
	  }
	pp_c_expression (ppi, TREE_OPERAND (e, 1));
      }
      break;

    case COMPLEX_CST:
    case VECTOR_CST:
    case COMPLEX_EXPR:
      pp_c_left_paren (ppi);
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      pp_left_brace (ppi);
      
      if (code == COMPLEX_CST)
	{
	  pp_c_expression (ppi, TREE_REALPART (e));
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_IMAGPART (e));
	}
      else if (code == VECTOR_CST)
	pp_c_expression_list (ppi, TREE_VECTOR_CST_ELTS (e));
      else if (code == COMPLEX_EXPR)
	{
	  pp_c_expression (ppi, TREE_OPERAND (e, 0));
	  pp_separate_with (ppi, ',');
	  pp_c_expression (ppi, TREE_OPERAND (e, 1));
	}
      
      pp_right_brace (ppi);
      break;

    case COMPOUND_LITERAL_EXPR:
      e = DECL_INITIAL (e);
      /* Fall through.  */
    case CONSTRUCTOR:
      pp_initializer (ppi, e);
      break;
      
    case VA_ARG_EXPR:
      pp_c_identifier (ppi, "__builtin_va_arg");
      pp_c_left_paren (ppi);
      pp_assignment_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      break;

    default:
      pp_primary_expression (ppi, e);
      break;
    }
}

/* Print out an expession-list; E is expected to be a TREE_LIST  */
void
pp_c_expression_list (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  for (; e != NULL_TREE; e = TREE_CHAIN (e))
    {
      pp_c_assignment_expression (ppi, TREE_VALUE (e));
      if (TREE_CHAIN (e))
	pp_separate_with (ppi, ',');
    }
}

static void
pp_c_unary_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
      pp_identifier (ppi, code == PREINCREMENT_EXPR ? "++" : "--");
      pp_c_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;
      
    case ADDR_EXPR:
    case INDIRECT_REF:
    case CONVERT_EXPR:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case CONJ_EXPR:
      if (code == ADDR_EXPR)
	pp_ampersand (ppi);
      else if (code == INDIRECT_REF)
	pp_star (ppi);
      else if (code == NEGATE_EXPR)
	pp_minus (ppi);
      else if (code == BIT_NOT_EXPR || code == CONJ_EXPR)
	pp_complement (ppi);
      else if (code == TRUTH_NOT_EXPR)
	pp_exclamation (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      pp_c_identifier (ppi, code == SIZEOF_EXPR ? "sizeof" : "__alignof__");
      pp_c_whitespace (ppi);
      if (TYPE_P (TREE_OPERAND (e, 0)))
	{
	  pp_c_left_paren (ppi);
	  pp_type_id (ppi, TREE_OPERAND (e, 0));
	  pp_c_right_paren (ppi);
	}
      else
	pp_c_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
      pp_c_identifier (ppi, code == REALPART_EXPR ? "__real__" : "__imag__");
      pp_c_whitespace (ppi);
      pp_unary_expression (ppi, TREE_OPERAND (e, 0));
      break;
      
    default:
      pp_postfix_expression (ppi, e);
      break;
    }
}

void
pp_c_cast_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == CONVERT_EXPR || TREE_CODE (e) == FLOAT_EXPR)
    {
      pp_c_left_paren (ppi);
      pp_type_id (ppi, TREE_TYPE (e));
      pp_c_right_paren (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 0));
    }
  else
    pp_unary_expression (ppi, e);
}

static void
pp_c_multiplicative_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
      pp_c_multiplicative_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == MULT_EXPR)
	pp_star (ppi);
      else if (code == TRUNC_DIV_EXPR)
	pp_slash (ppi);
      else
	pp_modulo (ppi);
      pp_c_whitespace (ppi);
      pp_c_cast_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_cast_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_additive_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
      pp_c_additive_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == PLUS_EXPR)
	pp_plus (ppi);
      else
	pp_minus (ppi);
      pp_c_whitespace (ppi);
      pp_multiplicative_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_multiplicative_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_shift_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      pp_c_shift_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      pp_identifier (ppi, code == LSHIFT_EXPR ? "<<" : ">>");
      pp_c_whitespace (ppi);
      pp_c_additive_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_additive_expression (ppi, e);
    }
}

static void
pp_c_relational_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
      pp_c_relational_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_whitespace (ppi);
      if (code == LT_EXPR)
	pp_less (ppi);
      else if (code == GT_EXPR)
	pp_greater (ppi);
      else if (code == LE_EXPR)
	pp_identifier (ppi, "<=");
      else if (code == GE_EXPR)
	pp_identifier (ppi, ">=");
      pp_c_whitespace (ppi);
      pp_c_shift_expression (ppi, TREE_OPERAND (e, 1));
      break;

    default:
      pp_c_shift_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_equality_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  enum tree_code code = TREE_CODE (e);
  switch (code)
    {
    case EQ_EXPR:
    case NE_EXPR:
      pp_c_equality_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_identifier (ppi, code == EQ_EXPR ? "==" : "!=");
      pp_c_whitespace (ppi);
      pp_c_relational_expression (ppi, TREE_OPERAND (e, 1));
      break;	
      
    default:
      pp_c_relational_expression (ppi, e);
      break;
    }
}

static inline void
pp_c_and_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == BIT_AND_EXPR)
    {
      pp_c_and_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_ampersand (ppi);
      pp_c_whitespace (ppi);
      pp_c_equality_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_equality_expression (ppi, e);
}

static inline void
pp_c_exclusive_or_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == BIT_XOR_EXPR)
    {
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_carret (ppi);
      pp_c_whitespace (ppi);
      pp_c_and_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_and_expression (ppi, e);
}

static inline void
pp_c_inclusive_or_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == BIT_IOR_EXPR)
    {
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_bar (ppi);
      pp_c_whitespace (ppi);
      pp_c_exclusive_or_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_exclusive_or_expression (ppi, e);
}

static inline void
pp_c_logical_and_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == TRUTH_ANDIF_EXPR)
    {
      pp_c_logical_and_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_identifier (ppi, "&&");
      pp_c_whitespace (ppi);
      pp_c_inclusive_or_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_inclusive_or_expression (ppi, e);
}

void
pp_c_logical_or_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == TRUTH_ORIF_EXPR)
    {
      pp_c_logical_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_identifier (ppi, "||");
      pp_c_whitespace (ppi);
      pp_c_logical_and_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_logical_and_expression (ppi, e);
}

static void
pp_c_conditional_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == COND_EXPR)
    {
      pp_c_logical_or_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_question (ppi);
      pp_c_whitespace (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_maybe_whitespace (ppi);
      pp_colon (ppi);
      pp_c_whitespace (ppi);
      pp_c_conditional_expression (ppi, TREE_OPERAND (e, 2));
    }
  else
    pp_c_logical_or_expression (ppi, e);
}


/* Pretty-print a C assignment-expression.  */
static void
pp_c_assignment_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  if (TREE_CODE (e) == MODIFY_EXPR || TREE_CODE (e) == INIT_EXPR)
    {
      pp_c_unary_expression (ppi, TREE_OPERAND (e, 0));
      pp_c_maybe_whitespace (ppi);
      pp_equal (ppi);
      pp_whitespace (ppi);
      pp_c_assignment_expression (ppi, TREE_OPERAND (e, 1));
    }
  else
    pp_c_conditional_expression (ppi, e);
}

/* Pretty-print an expression.  */
void
pp_c_expression (ppi, e)
     c_pretty_print_info *ppi;
     tree e;
{
  switch (TREE_CODE (e))
    {
    case INTEGER_CST:
      pp_c_integer_literal (ppi, e);
      break;
      
    case REAL_CST:
      pp_c_real_literal (ppi, e);
      break;

    case STRING_CST:
      pp_c_string_literal (ppi, e);
      break;
      
    case FUNCTION_DECL:
    case VAR_DECL:
    case CONST_DECL:
    case PARM_DECL:
    case RESULT_DECL:
    case FIELD_DECL:
    case LABEL_DECL:
    case ERROR_MARK:
    case TARGET_EXPR:
    case STMT_EXPR:
      pp_c_primary_expression (ppi, e);
      break;

    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case ARROW_EXPR:
    case ARRAY_REF:
    case CALL_EXPR:
    case COMPONENT_REF:
    case COMPLEX_CST:
    case VECTOR_CST:
    case ABS_EXPR:
    case FFS_EXPR:
    case CONSTRUCTOR:
    case COMPOUND_LITERAL_EXPR:
    case COMPLEX_EXPR:
    case VA_ARG_EXPR:
      pp_c_postfix_expression (ppi, e);
      break;

    case CONJ_EXPR:
    case ADDR_EXPR:
    case INDIRECT_REF:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      pp_c_unary_expression (ppi, e);
      break;

    case CONVERT_EXPR:
    case FLOAT_EXPR:
      pp_c_cast_expression (ppi, e);
      break;

    case MULT_EXPR:
    case TRUNC_MOD_EXPR:
    case TRUNC_DIV_EXPR:
      pp_c_multiplicative_expression (ppi, e);
      break;

    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
      pp_c_shift_expression (ppi, e);
      break;

    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
      pp_c_relational_expression (ppi, e);
      break;

    case BIT_AND_EXPR:
      pp_c_and_expression (ppi, e);
      break;

    case BIT_XOR_EXPR:
      pp_c_exclusive_or_expression (ppi, e);
      break;

    case BIT_IOR_EXPR:
      pp_c_inclusive_or_expression (ppi, e);
      break;

    case TRUTH_ANDIF_EXPR:
      pp_c_logical_and_expression (ppi, e);
      break;

    case TRUTH_ORIF_EXPR:
      pp_c_logical_or_expression (ppi, e);
      break;

    case COND_EXPR:
      pp_c_conditional_expression (ppi, e);
      break;

    case MODIFY_EXPR:
    case INIT_EXPR:
      pp_c_assignment_expression (ppi, e);
      break;

    case NOP_EXPR:
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      break;

    case COMPOUND_EXPR:
      pp_c_left_paren (ppi);
      pp_c_expression (ppi, TREE_OPERAND (e, 0));
      pp_separate_with (ppi, ',');
      pp_assignment_expression (ppi, TREE_OPERAND (e, 1));
      pp_c_right_paren (ppi);
      break;
		     

    default:
      pp_unsupported_tree (ppi, e);
      break;
    }
}

