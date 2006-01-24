/*
 * Format String Generator for IDL Compiler
 *
 * Copyright 2005 Eric Kohl
 * Copyright 2005 Robert Shearman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>

#include "widl.h"
#include "utils.h"
#include "parser.h"
#include "header.h"
#include "windef.h"

#include "widl.h"
#include "typegen.h"

static const func_t *current_func;

static int print_file(FILE *file, int indent, const char *format, ...)
{
    va_list va;
    int i, r;

    if (!file) return 0;

    va_start(va, format);
    for (i = 0; i < indent; i++)
        fprintf(file, "    ");
    r = vfprintf(file, format, va);
    va_end(va);
    return r;
}

static inline int type_has_ref(const type_t *type)
{
    return (type->type == 0 && type->ref);
}

static inline int is_base_type(unsigned char type)
{
    switch (type)
    {
    case RPC_FC_BYTE:
    case RPC_FC_CHAR:
    case RPC_FC_USMALL:
    case RPC_FC_SMALL:
    case RPC_FC_WCHAR:
    case RPC_FC_USHORT:
    case RPC_FC_SHORT:
    case RPC_FC_ULONG:
    case RPC_FC_LONG:
    case RPC_FC_HYPER:
    case RPC_FC_IGNORE:
    case RPC_FC_FLOAT:
    case RPC_FC_DOUBLE:
    case RPC_FC_ENUM16:
    case RPC_FC_ENUM32:
    case RPC_FC_ERROR_STATUS_T:
        return TRUE;

    default:
        return FALSE;
    }
}

static inline int is_string_type(const attr_t *attrs, int ptr_level, const expr_t *array)
{
    return (is_attr(attrs, ATTR_STRING) &&
            ((ptr_level == 1 && !array) || (ptr_level == 0 && array)));
}

static inline int is_array_type(const attr_t *attrs, int ptr_level, const expr_t *array)
{
    return ((ptr_level == 1 && !array && is_attr(attrs, ATTR_SIZEIS)) ||
            (ptr_level == 0 && array));
}

static size_t write_procformatstring_var(FILE *file, int indent,
    const var_t *var, int is_return, unsigned int *type_offset)
{
    size_t size;
    int ptr_level = var->ptr_level;
    const type_t *type = var->type;

    int is_in = is_attr(var->attrs, ATTR_IN);
    int is_out = is_attr(var->attrs, ATTR_OUT);

    if (!is_in && !is_out) is_in = TRUE;

    if (ptr_level == 0 && type_has_ref(type))
        type = type->ref;

    if (ptr_level == 0 && !var->array && is_base_type(type->type))
    {
        if (is_return)
            print_file(file, indent, "0x53,    /* FC_RETURN_PARAM_BASETYPE */\n");
        else
            print_file(file, indent, "0x4e,    /* FC_IN_PARAM_BASETYPE */\n");

        switch(type->type)
        {
#define CASE_BASETYPE(fctype) \
        case RPC_##fctype: \
            print_file(file, indent, "0x%02x,    /* " #fctype " */\n", RPC_##fctype); \
            size = 2; /* includes param type prefix */ \
            break

        CASE_BASETYPE(FC_BYTE);
        CASE_BASETYPE(FC_CHAR);
        CASE_BASETYPE(FC_WCHAR);
        CASE_BASETYPE(FC_USHORT);
        CASE_BASETYPE(FC_SHORT);
        CASE_BASETYPE(FC_ULONG);
        CASE_BASETYPE(FC_LONG);
        CASE_BASETYPE(FC_HYPER);
        CASE_BASETYPE(FC_IGNORE);
        CASE_BASETYPE(FC_USMALL);
        CASE_BASETYPE(FC_SMALL);
        CASE_BASETYPE(FC_FLOAT);
        CASE_BASETYPE(FC_DOUBLE);
        CASE_BASETYPE(FC_ERROR_STATUS_T);
#undef CASE_BASETYPE
        default:
            error("Unknown/unsupported type: %s (0x%02x)\n", var->name, type->type);
            size = 0;
        }
    }
    else
    {
        if (is_return)
            print_file(file, indent, "0x52,    /* FC_RETURN_PARAM */\n");
        else if (is_in && is_out)
            print_file(file, indent, "0x50,    /* FC_IN_OUT_PARAM */\n");
        else if (is_out)
            print_file(file, indent, "0x51,    /* FC_OUT_PARAM */\n");
        else
            print_file(file, indent, "0x4d,    /* FC_IN_PARAM */\n");

        print_file(file, indent, "0x01,\n");
        print_file(file, indent, "NdrFcShort(0x%x),\n", *type_offset);
        size = 4; /* includes param type prefix */
    }
    *type_offset += get_size_typeformatstring_var(var);
    return size;
}

void write_procformatstring(FILE *file, type_t *iface)
{
    int indent = 0;
    var_t *var;
    unsigned int type_offset = 2;

    print_file(file, indent, "static const MIDL_PROC_FORMAT_STRING __MIDL_ProcFormatString =\n");
    print_file(file, indent, "{\n");
    indent++;
    print_file(file, indent, "0,\n");
    print_file(file, indent, "{\n");
    indent++;

    if (iface->funcs)
    {
        func_t *func = iface->funcs;
        while (NEXT_LINK(func)) func = NEXT_LINK(func);
        for (; func; func = PREV_LINK(func))
        {
            /* emit argument data */
            if (func->args)
            {
                var = func->args;
                while (NEXT_LINK(var)) var = NEXT_LINK(var);
                while (var)
                {
                    write_procformatstring_var(file, indent, var, FALSE,
                                               &type_offset);

                    var = PREV_LINK(var);
                }
            }

            /* emit return value data */
            var = func->def;
            if (is_void(var->type, NULL))
            {
                print_file(file, indent, "0x5b,    /* FC_END */\n");
                print_file(file, indent, "0x5c,    /* FC_PAD */\n");
            }
            else
                write_procformatstring_var(file, indent, var, TRUE,
                                           &type_offset);
        }
    }

    print_file(file, indent, "0x0\n");
    indent--;
    print_file(file, indent, "}\n");
    indent--;
    print_file(file, indent, "};\n");
    print_file(file, indent, "\n");
}

/* write conformance / variance descriptor */
/* FIXME: only works for top-level variables at the moment */
static size_t write_conf_or_var_desc(FILE *file, const func_t *func, const expr_t *expr)
{
    unsigned char operator_type = 0;
    const char *operator_string = "no operators";
    const expr_t *subexpr = expr;

    if (!file) return 4; /* optimisation for sizing pass */

    switch (subexpr->type)
    {
    case EXPR_PPTR:
        subexpr = subexpr->ref;
        operator_type = RPC_FC_DEREFERENCE;
        operator_string = "FC_DEREFERENCE";
        break;
    case EXPR_DIV:
        if (subexpr->u.ext->is_const && (subexpr->u.ext->cval == 2))
        {
            subexpr = subexpr->ref;
            operator_type = RPC_FC_DIV_2;
            operator_string = "FC_DIV_2";
        }
        break;
    case EXPR_MUL:
        if (subexpr->u.ext->is_const && (subexpr->u.ext->cval == 2))
        {
            subexpr = subexpr->ref;
            operator_type = RPC_FC_MULT_2;
            operator_string = "FC_MULT_2";
        }
        break;
    case EXPR_SUB:
        if (subexpr->u.ext->is_const && (subexpr->u.ext->cval == 1))
        {
            subexpr = subexpr->ref;
            operator_type = RPC_FC_SUB_1;
            operator_string = "FC_SUB_1";
        }
        break;
    case EXPR_ADD:
        if (subexpr->u.ext->is_const && (subexpr->u.ext->cval == 1))
        {
            subexpr = subexpr->ref;
            operator_type = RPC_FC_ADD_1;
            operator_string = "FC_ADD_1";
        }
        break;
    default:
        break;
    }

    if (subexpr->type == EXPR_IDENTIFIER)
    {
        if (func)
        {
            size_t stack_offset;
            const var_t *var = func->args;
            unsigned char param_type = 0;
            const char *param_type_string = NULL;
            const type_t *conformance_type = NULL;

            while (NEXT_LINK(var)) var = NEXT_LINK(var);
            for (stack_offset = 0; var; stack_offset += sizeof(void *), var = PREV_LINK(var))
            {
                if (!strcmp(var->name, subexpr->u.sval))
                {
                    conformance_type = var->type;
                    break;
                }
            }
            if (!conformance_type)
                error("write_conf_or_var_desc: couldn't find variable %s\n",
                    subexpr->u.sval);

            while (type_has_ref(conformance_type))
                conformance_type = conformance_type->ref;

            switch (conformance_type->type)
            {
            case RPC_FC_CHAR:
            case RPC_FC_SMALL:
                param_type = RPC_FC_SMALL;
                param_type_string = "FC_SMALL";
                break;
            case RPC_FC_BYTE:
            case RPC_FC_USMALL:
                param_type = RPC_FC_USMALL;
                param_type_string = "FC_USMALL";
                break;
            case RPC_FC_WCHAR:
            case RPC_FC_SHORT:
                param_type = RPC_FC_SHORT;
                param_type_string = "FC_SHORT";
                break;
            case RPC_FC_USHORT:
                param_type = RPC_FC_USHORT;
                param_type_string = "FC_USHORT";
                break;
            case RPC_FC_LONG:
                param_type = RPC_FC_LONG;
                param_type_string = "FC_LONG";
                break;
            case RPC_FC_ULONG:
                param_type = RPC_FC_ULONG;
                param_type_string = "FC_ULONG";
                break;
            default:
                error("write_conf_or_var_desc: conformance variable type not supported 0x%x\n",
                    conformance_type->type);
            }

            print_file(file, 2, "0x%x, /* Corr desc: parameter, %s */\n",
                    RPC_FC_TOP_LEVEL_CONFORMANCE | param_type,
                    param_type_string);
            print_file(file, 2, "0x%x, /* %s */\n", operator_type, operator_string);
            print_file(file, 2, "0x%x, /* x86 stack size / offset = %d */\n", stack_offset, stack_offset);
        }
        else
            error("write_conf_or_var_desc: not supported for non-functions yet\n");
    }
    else
        error("write_conf_or_var_desc: expression type %d\n", subexpr->type);
    return 4;
}

static size_t type_memsize(const type_t *t, int ptr_level, const expr_t *array);

static size_t fields_memsize(const var_t *v)
{
    size_t size = 0;
    const var_t *first = v;
    if (!v) return 0;
    while (NEXT_LINK(v)) v = NEXT_LINK(v);
    while (v) {
        size += type_memsize(v->type, v->ptr_level, v->array);
        if (v == first) break;
        v = PREV_LINK(v);
    }
    return size;
}

static size_t type_memsize(const type_t *t, int ptr_level, const expr_t *array)
{
    size_t size = 0;

    if (ptr_level)
        return sizeof(void *);

    switch (t->type)
    {
    case RPC_FC_BYTE:
    case RPC_FC_CHAR:
    case RPC_FC_USMALL:
    case RPC_FC_SMALL:
        size = 1;
        break;
    case RPC_FC_WCHAR:
    case RPC_FC_USHORT:
    case RPC_FC_SHORT:
    case RPC_FC_ENUM16:
        size = 2;
        break;
    case RPC_FC_ULONG:
    case RPC_FC_LONG:
    case RPC_FC_ERROR_STATUS_T:
    case RPC_FC_ENUM32:
    case RPC_FC_FLOAT:
        size = 4;
        break;
    case RPC_FC_HYPER:
    case RPC_FC_DOUBLE:
        size = 8;
        break;
    case RPC_FC_STRUCT:
    case RPC_FC_CVSTRUCT:
    case RPC_FC_CPSTRUCT:
    case RPC_FC_CSTRUCT:
    case RPC_FC_PSTRUCT:
    case RPC_FC_BOGUS_STRUCT:
    case RPC_FC_ENCAPSULATED_UNION:
    case RPC_FC_NON_ENCAPSULATED_UNION:
        size = fields_memsize(t->fields);
        break;
    default:
        error("type_memsize: Unknown type %d", t->type);
        size = 0;
    }

    if (array && array->is_const)
        size *= array->cval;

    return size;
}

static size_t write_string_tfs(FILE *file, const attr_t *attrs,
                               const type_t *type, const expr_t *array,
                               const char *name)
{
    const expr_t *size_is = get_attrp(attrs, ATTR_SIZEIS);
    int has_size = size_is && (size_is->type != EXPR_VOID);

    if ((type->type != RPC_FC_CHAR) && (type->type != RPC_FC_WCHAR))
    {
        error("write_string_tfs: Unimplemented for type 0x%x of name: %s\n", type->type, name);
        return 0;
    }

    if (array && array->is_const)
    {
        size_t typestring_size;

        if (array->cval > USHRT_MAX)
            error("array size for parameter %s exceeds %d bytes by %ld bytes\n",
                  name, USHRT_MAX, array->cval - USHRT_MAX);

        if (type->type == RPC_FC_CHAR)
            print_file(file, 2, "0x%x, /* FC_CSTRING */\n", RPC_FC_C_CSTRING);
        else
            print_file(file, 2, "0x%x, /* FC_WSTRING */\n", RPC_FC_C_WSTRING);
        print_file(file, 2, "0x%x, /* FC_PAD */\n", RPC_FC_PAD);
        typestring_size = 2;

        print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", array->cval, array->cval);
        typestring_size += 2;

        return typestring_size;
    }
    else if (has_size)
    {
        size_t typestring_size;

        if (type->type == RPC_FC_CHAR)
            print_file(file, 2, "0x%x, /* FC_C_CSTRING */\n", RPC_FC_C_CSTRING);
        else
            print_file(file, 2, "0x%x, /* FC_C_WSTRING */\n", RPC_FC_C_WSTRING);
        print_file(file, 2, "0x%x, /* FC_STRING_SIZED */\n", RPC_FC_STRING_SIZED);
        typestring_size = 2;

        typestring_size += write_conf_or_var_desc(file, current_func, size_is);

        return typestring_size;
    }
    else
    {
        size_t typestring_size;

        if (type->type == RPC_FC_CHAR)
            print_file(file, 2, "0x%x, /* FC_C_CSTRING */\n", RPC_FC_C_CSTRING);
        else
            print_file(file, 2, "0x%x, /* FC_C_WSTRING */\n", RPC_FC_C_WSTRING);
        print_file(file, 2, "0x%x, /* FC_PAD */\n", RPC_FC_PAD);
        typestring_size = 2;

        return typestring_size;
    }
}

static size_t write_array_tfs(FILE *file, const attr_t *attrs,
                              const type_t *type, const expr_t *array,
                              const char *name)
{
    const expr_t *length_is = get_attrp(attrs, ATTR_LENGTHIS);
    const expr_t *size_is = get_attrp(attrs, ATTR_SIZEIS);
    int has_length = length_is && (length_is->type != EXPR_VOID);
    int has_size = size_is && (size_is->type != EXPR_VOID) && !array->is_const;

    /* FIXME: need to analyse type for pointers */

    if (NEXT_LINK(array)) /* multi-dimensional array */
    {
        error("write_array_tfs: Multi-dimensional arrays not implemented yet (param %s)\n", name);
        return 0;
    }
    else
    {
        if (!has_length && !has_size)
        {
            /* fixed array */
            size_t typestring_size;
            size_t size = type_memsize(type, 0, array);
            if (size < USHRT_MAX)
            {
                print_file(file, 2, "0x%x, /* FC_SMFARRAY */\n", RPC_FC_SMFARRAY);
                /* alignment */
                print_file(file, 2, "0x%x, /* 0 */\n", 0);
                /* size */
                print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", size, size);
                typestring_size = 4;
            }
            else
            {
                print_file(file, 2, "0x%x, /* FC_LGFARRAY */\n", RPC_FC_LGFARRAY);
                /* alignment */
                print_file(file, 2, "0x%x, /* 0 */\n", 0);
                /* size */
                print_file(file, 2, "NdrFcLong(0x%x), /* %d */\n", size, size);
                typestring_size = 6;
            }

            /* FIXME: write out pointer descriptor if necessary */

            print_file(file, 2, "0x0, /* FIXME: write out conversion data */\n");
            print_file(file, 2, "FC_END,\n");
            typestring_size += 2;

            return typestring_size;
        }
        else if (has_length && !has_size)
        {
            /* varying array */
            size_t typestring_size;
            size_t element_size = type_memsize(type, 0, NULL);
            size_t elements = array->cval;
            size_t total_size = element_size * elements;

            if (total_size < USHRT_MAX)
            {
                print_file(file, 2, "0x%x, /* FC_SMVARRAY */\n", RPC_FC_SMVARRAY);
                /* alignment */
                print_file(file, 2, "0x%x, /* 0 */\n", 0);
                /* total size */
                print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", total_size, total_size);
                /* number of elements */
                print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", elements, elements);
                typestring_size = 6;
            }
            else
            {
                print_file(file, 2, "0x%x, /* FC_LGVARRAY */\n", RPC_FC_LGVARRAY);
                /* alignment */
                print_file(file, 2, "0x%x, /* 0 */\n", 0);
                /* total size */
                print_file(file, 2, "NdrFcLong(0x%x), /* %d */\n", total_size, total_size);
                /* number of elements */
                print_file(file, 2, "NdrFcLong(0x%x), /* %d */\n", elements, elements);
                typestring_size = 10;
            }
            /* element size */
            print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", element_size, element_size);
            typestring_size += 2;

            typestring_size += write_conf_or_var_desc(file, current_func, length_is);

            /* FIXME: write out pointer descriptor if necessary */

            print_file(file, 2, "0x0, /* FIXME: write out conversion data */\n");
            print_file(file, 2, "FC_END,\n");
            typestring_size += 2;

            return typestring_size;
        }
        else if (!has_length && has_size)
        {
            /* conformant array */
            size_t typestring_size;
            size_t element_size = type_memsize(type, 0, NULL);

            print_file(file, 2, "0x%x, /* FC_CARRAY */\n", RPC_FC_CARRAY);
            /* alignment */
            print_file(file, 2, "0x%x, /* 0 */\n", 0);
            /* element size */
            print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", element_size, element_size);
            typestring_size = 4;

            typestring_size += write_conf_or_var_desc(file, current_func, size_is);

            /* FIXME: write out pointer descriptor if necessary */

            print_file(file, 2, "0x0, /* FIXME: write out conversion data */\n");
            print_file(file, 2, "FC_END,\n");
            typestring_size += 2;

            return typestring_size;
        }
        else
        {
            /* conformant varying array */
            size_t typestring_size;
            size_t element_size = type_memsize(type, 0, NULL);

            print_file(file, 2, "0x%x, /* FC_CARRAY */\n", RPC_FC_CARRAY);
            /* alignment */
            print_file(file, 2, "0x%x, /* 0 */\n", 0);
            /* element size */
            print_file(file, 2, "NdrFcShort(0x%x), /* %d */\n", element_size, element_size);
            typestring_size = 4;

            typestring_size += write_conf_or_var_desc(file, current_func, size_is);
            typestring_size += write_conf_or_var_desc(file, current_func, length_is);

            /* FIXME: write out pointer descriptor if necessary */

            print_file(file, 2, "0x0, /* FIXME: write out conversion data */\n");
            print_file(file, 2, "FC_END,\n");
            typestring_size += 2;

            return typestring_size;
        }
    }
}

static size_t write_struct_tfs(FILE *file, const type_t *type, const char *name)
{
    error("write_struct_tfs: Unimplemented\n");
    return 0;
}

static size_t write_union_tfs(FILE *file, const attr_t *attrs, const type_t *type, const char *name)
{
    error("write_union_tfs: Unimplemented\n");
    return 0;
}

static size_t write_typeformatstring_var(FILE *file, int indent,
    const var_t *var)
{
    const type_t *type = var->type;
    int ptr_level = var->ptr_level;

    while (TRUE)
    {
        if (is_string_type(var->attrs, ptr_level, var->array))
            return write_string_tfs(file, var->attrs, type, var->array, var->name);

        if (is_array_type(var->attrs, ptr_level, var->array))
            return write_array_tfs(file, var->attrs, type, var->array, var->name);

        if (ptr_level == 0)
        {
            /* follow reference if the type has one */
            if (type_has_ref(type))
            {
                type = type->ref;
                /* FIXME: get new ptr_level from type */
                continue;
            }

            /* basic types don't need a type format string */
            if (is_base_type(type->type))
                return 0;

            switch (type->type)
            {
            case RPC_FC_STRUCT:
            case RPC_FC_PSTRUCT:
            case RPC_FC_CSTRUCT:
            case RPC_FC_CPSTRUCT:
            case RPC_FC_CVSTRUCT:
            case RPC_FC_BOGUS_STRUCT:
                return write_struct_tfs(file, type, var->name);
            case RPC_FC_ENCAPSULATED_UNION:
            case RPC_FC_NON_ENCAPSULATED_UNION:
                return write_union_tfs(file, var->attrs, type, var->name);
            default:
                error("write_typeformatstring_var: Unsupported type 0x%x for variable %s\n", type->type, var->name);
            }
        }
        else if (ptr_level == 1 && !type_has_ref(type))
        {
            int pointer_type = get_attrv(var->attrs, ATTR_POINTERTYPE);
            if (!pointer_type) pointer_type = RPC_FC_RP;

            /* special case for pointers to base types */
            switch (type->type)
            {
#define CASE_BASETYPE(fctype) \
            case RPC_##fctype: \
                print_file(file, indent, "0x%x, 0x08,    /* %s [simple_pointer] */\n", \
                           pointer_type, \
                           pointer_type == RPC_FC_FP ? "FC_FP" : (pointer_type == RPC_FC_UP ? "FC_UP" : "FC_RP")); \
                print_file(file, indent, "0x%02x,    /* " #fctype " */\n", RPC_##fctype); \
                print_file(file, indent, "0x5c,          /* FC_PAD */\n"); \
                return 4
            CASE_BASETYPE(FC_BYTE);
            CASE_BASETYPE(FC_CHAR);
            CASE_BASETYPE(FC_SMALL);
            CASE_BASETYPE(FC_USMALL);
            CASE_BASETYPE(FC_WCHAR);
            CASE_BASETYPE(FC_SHORT);
            CASE_BASETYPE(FC_USHORT);
            CASE_BASETYPE(FC_LONG);
            CASE_BASETYPE(FC_ULONG);
            CASE_BASETYPE(FC_FLOAT);
            CASE_BASETYPE(FC_HYPER);
            CASE_BASETYPE(FC_DOUBLE);
            CASE_BASETYPE(FC_ENUM16);
            CASE_BASETYPE(FC_ENUM32);
            CASE_BASETYPE(FC_IGNORE);
            CASE_BASETYPE(FC_ERROR_STATUS_T);
            default:
                error("write_typeformatstring_var: Unknown/unsupported type: %s (0x%02x)\n", var->name, type->type);
                return 0;
            }
        }
        error("write_typeformatstring_var: Pointer level %d not supported for variable %s\n", ptr_level, var->name);
        return 0;
    }
}


void write_typeformatstring(FILE *file, type_t *iface)
{
    int indent = 0;
    var_t *var;

    print_file(file, indent, "static const MIDL_TYPE_FORMAT_STRING __MIDL_TypeFormatString =\n");
    print_file(file, indent, "{\n");
    indent++;
    print_file(file, indent, "0,\n");
    print_file(file, indent, "{\n");
    indent++;
    print_file(file, indent, "NdrFcShort(0x0),\n");

    if (iface->funcs)
    {
        func_t *func = iface->funcs;
        while (NEXT_LINK(func)) func = NEXT_LINK(func);
        for (; func; func = PREV_LINK(func))
        {
            current_func = func;
            if (func->args)
            {
                var = func->args;
                while (NEXT_LINK(var)) var = NEXT_LINK(var);
                while (var)
                {
                    write_typeformatstring_var(file, indent, var);
                    var = PREV_LINK(var);
                }
            }
        }
    }

    print_file(file, indent, "0x0\n");
    indent--;
    print_file(file, indent, "}\n");
    indent--;
    print_file(file, indent, "};\n");
    print_file(file, indent, "\n");
}

static unsigned int get_required_buffer_size_type(
    const type_t *type, int ptr_level, const expr_t *array,
    const char *name, unsigned int *alignment)
{
    *alignment = 0;
    if (ptr_level == 0 && !array && !type_has_ref(type))
    {
        switch (type->type)
        {
        case RPC_FC_BYTE:
        case RPC_FC_CHAR:
        case RPC_FC_USMALL:
        case RPC_FC_SMALL:
            return 1;

        case RPC_FC_WCHAR:
        case RPC_FC_USHORT:
        case RPC_FC_SHORT:
            *alignment = 2;
            return 2;

        case RPC_FC_ULONG:
        case RPC_FC_LONG:
        case RPC_FC_FLOAT:
        case RPC_FC_ERROR_STATUS_T:
            *alignment = 4;
            return 4;

        case RPC_FC_HYPER:
        case RPC_FC_DOUBLE:
            *alignment = 8;
            return 8;

        case RPC_FC_STRUCT:
        {
            size_t size = 0;
            const var_t *field;
            for (field = type->fields; field; field = NEXT_LINK(field))
            {
                unsigned int alignment;
                size += get_required_buffer_size_type(
                    field->type, field->ptr_level, field->array, field->name,
                    &alignment);
            }
            return size;
        }

        default:
            error("get_required_buffer_size: Unknown/unsupported type: %s (0x%02x)\n", name, type->type);
            return 0;
        }
    }
    if (ptr_level == 0 && type_has_ref(type))
        return get_required_buffer_size_type(type->ref, 0 /* FIXME */, array, name, alignment);
    return 0;
}

unsigned int get_required_buffer_size(const var_t *var, unsigned int *alignment)
{
    return get_required_buffer_size_type(var->type, var->ptr_level, var->array, var->name, alignment);
}

void marshall_arguments(FILE *file, int indent, func_t *func,
                        unsigned int *type_offset, enum pass pass)
{
    unsigned int last_size = 0;
    var_t *var;

    if (!func->args)
        return;

    var = func->args;
    while (NEXT_LINK(var)) var = NEXT_LINK(var);
    for (; var; *type_offset += get_size_typeformatstring_var(var), var = PREV_LINK(var))
    {
        int in_attr = is_attr(var->attrs, ATTR_IN);
        int out_attr = is_attr(var->attrs, ATTR_OUT);

        if (!in_attr && !out_attr)
            in_attr = 1;

        switch (pass)
        {
        case PASS_IN:
            if (!in_attr)
                continue;
            break;
        case PASS_OUT:
            if (!out_attr)
                continue;
            break;
        case PASS_RETURN:
            break;
        }

        if (is_string_type(var->attrs, var->ptr_level, var->array))
        {
            if (var->array && var->array->is_const)
                print_file(file, indent,
                           "NdrNonConformantStringMarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d]);\n",
                           var->name, *type_offset);
            else
                print_file(file, indent,
                           "NdrConformantStringMarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d]);\n",
                           var->name, *type_offset);
            last_size = 1;
        }
        else if (is_array_type(var->attrs, var->ptr_level, var->array))
        {
            const expr_t *length_is = get_attrp(var->attrs, ATTR_LENGTHIS);
            const expr_t *size_is = get_attrp(var->attrs, ATTR_SIZEIS);
            const char *array_type;
            int has_length = length_is && (length_is->type != EXPR_VOID);
            int has_size = size_is && (size_is->type != EXPR_VOID) && !var->array->is_const;

            if (NEXT_LINK(var->array)) /* multi-dimensional array */
                array_type = "ComplexArray";
            else
            {
                if (!has_length && !has_size)
                    array_type = "FixedArray";
                else if (has_length && !has_size)
                {
                    print_file(file, indent, "_StubMsg.Offset = (unsigned long)0;\n"); /* FIXME */
                    print_file(file, indent, "_StubMsg.ActualCount = (unsigned long)");
                    write_expr(file, length_is, 1);
                    fprintf(file, ";\n\n");
                    array_type = "VaryingArray";
                }
                else if (!has_length && has_size)
                {
                    print_file(file, indent, "_StubMsg.MaxCount = (unsigned long)");
                    write_expr(file, size_is ? size_is : var->array, 1);
                    fprintf(file, ";\n\n");
                    array_type = "ConformantArray";
                }
                else
                {
                    print_file(file, indent, "_StubMsg.MaxCount = (unsigned long)");
                    write_expr(file, size_is ? size_is : var->array, 1);
                    fprintf(file, ";\n");
                    print_file(file, indent, "_StubMsg.Offset = (unsigned long)0;\n"); /* FIXME */
                    print_file(file, indent, "_StubMsg.ActualCount = (unsigned long)");
                    write_expr(file, length_is, 1);
                    fprintf(file, ";\n\n");
                    array_type = "ConformantVaryingArray";
                }
            }

            print_file(file, indent,
                       "Ndr%sMarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d]);\n",
                       array_type, var->name, *type_offset);
            last_size = 1;
        }
        else if (var->ptr_level == 0 && is_base_type(var->type->type))
        {
            unsigned int size;
            unsigned int alignment = 0;
            switch (var->type->type)
            {
            case RPC_FC_BYTE:
            case RPC_FC_CHAR:
            case RPC_FC_SMALL:
            case RPC_FC_USMALL:
                size = 1;
                alignment = 0;
                break;

            case RPC_FC_WCHAR:
            case RPC_FC_USHORT:
            case RPC_FC_SHORT:
                size = 2;
                if (last_size != 0 && last_size < 2)
                    alignment = (2 - last_size);
                break;

            case RPC_FC_ULONG:
            case RPC_FC_LONG:
            case RPC_FC_FLOAT:
            case RPC_FC_ERROR_STATUS_T:
                size = 4;
                if (last_size != 0 && last_size < 4)
                    alignment = (4 - last_size);
                break;

            case RPC_FC_HYPER:
            case RPC_FC_DOUBLE:
                size = 8;
                if (last_size != 0 && last_size < 4)
                    alignment = (4 - last_size);
                break;

            default:
                error("marshall_arguments: Unsupported type: %s (0x%02x, ptr_level: 0)\n", var->name, var->type->type);
                size = 0;
            }

            if (alignment != 0)
                print_file(file, indent, "_StubMsg.Buffer += %u;\n", alignment);

            print_file(file, indent, "*(");
            write_type(file, var->type, var, var->tname);
            fprintf(file, " *)_StubMsg.Buffer = ");
            write_name(file, var);
            fprintf(file, ";\n");
            fprintf(file, "_StubMsg.Buffer += sizeof(");
            write_type(file, var->type, var, var->tname);
            fprintf(file, ");\n");

            last_size = size;
        }
        else if (var->ptr_level == 0)
        {
            const char *ndrtype;

            switch (var->type->type)
            {
            case RPC_FC_STRUCT:
                ndrtype = "SimpleStruct";
                break;
            case RPC_FC_CSTRUCT:
            case RPC_FC_CPSTRUCT:
                ndrtype = "ConformantStruct";
                break;
            case RPC_FC_CVSTRUCT:
                ndrtype = "ConformantVaryingStruct";
                break;
            case RPC_FC_BOGUS_STRUCT:
                ndrtype = "ComplexStruct";
                break;
            default:
                error("marshall_arguments: Unsupported type: %s (0x%02x, ptr_level: %d)\n",
                    var->name, var->type->type, var->ptr_level);
                ndrtype = NULL;
            }

            print_file(file, indent,
                "Ndr%sMarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d]);\n",
                ndrtype, var->name, *type_offset);
            last_size = 1;
        }
        else
        {
            print_file(file, indent,
                       "NdrPointerMarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d]);\n",
                       var->name, *type_offset);
            last_size = 1;
        }
        fprintf(file, "\n");
    }
}

void unmarshall_arguments(FILE *file, int indent, func_t *func,
                          unsigned int *type_offset, enum pass pass)
{
    unsigned int last_size = 0;
    var_t *var;

    if (!func->args)
        return;

    var = func->args;
    while (NEXT_LINK(var)) var = NEXT_LINK(var);
    for (; var; *type_offset += get_size_typeformatstring_var(var), var = PREV_LINK(var))
    {
        int in_attr = is_attr(var->attrs, ATTR_IN);
        int out_attr = is_attr(var->attrs, ATTR_OUT);

        if (!in_attr && !out_attr)
            in_attr = 1;

        switch (pass)
        {
        case PASS_IN:
            if (!in_attr)
                continue;
            break;
        case PASS_OUT:
            if (!out_attr)
                continue;
            break;
        case PASS_RETURN:
            break;
        }

        if (is_string_type(var->attrs, var->ptr_level, var->array))
        {
            if (var->array && var->array->is_const)
                print_file(file, indent,
                           "NdrNonConformantStringUnmarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d], 0);\n",
                           var->name, *type_offset);
            else
                print_file(file, indent,
                           "NdrConformantStringUnmarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d], 0);\n",
                           var->name, *type_offset);
            last_size = 1;
        }
        else if (is_array_type(var->attrs, var->ptr_level, var->array))
        {
            const expr_t *length_is = get_attrp(var->attrs, ATTR_LENGTHIS);
            const expr_t *size_is = get_attrp(var->attrs, ATTR_SIZEIS);
            const char *array_type;
            int has_length = length_is && (length_is->type != EXPR_VOID);
            int has_size = size_is && (size_is->type != EXPR_VOID) && !var->array->is_const;

            if (NEXT_LINK(var->array)) /* multi-dimensional array */
                array_type = "ComplexArray";
            else
            {
                if (!has_length && !has_size)
                    array_type = "FixedArray";
                else if (has_length && !has_size)
                    array_type = "VaryingArray";
                else if (!has_length && has_size)
                    array_type = "ConformantArray";
                else
                    array_type = "ConformantVaryingArray";
            }

            print_file(file, indent,
                       "Ndr%sUnmarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d], 0);\n",
                       array_type, var->name, *type_offset);
            last_size = 1;
        }
        else if (var->ptr_level == 0 && is_base_type(var->type->type))
        {
            unsigned int size;
            unsigned int alignment = 0;

            switch (var->type->type)
            {
            case RPC_FC_BYTE:
            case RPC_FC_CHAR:
            case RPC_FC_SMALL:
            case RPC_FC_USMALL:
                size = 1;
                alignment = 0;
                break;

            case RPC_FC_WCHAR:
            case RPC_FC_USHORT:
            case RPC_FC_SHORT:
                size = 2;
                if (last_size != 0 && last_size < 2)
                    alignment = (2 - last_size);
                break;

            case RPC_FC_ULONG:
            case RPC_FC_LONG:
            case RPC_FC_FLOAT:
            case RPC_FC_ERROR_STATUS_T:
                size = 4;
                if (last_size != 0 && last_size < 4)
                    alignment = (4 - last_size);
                break;

            case RPC_FC_HYPER:
            case RPC_FC_DOUBLE:
                size = 8;
                if (last_size != 0 && last_size < 4)
                    alignment = (4 - last_size);
                break;

            default:
                error("unmarshall_arguments: Unsupported type: %s (0x%02x, ptr_level: 0)\n", var->name, var->type->type);
                size = 0;
            }

            if (alignment != 0)
                print_file(file, indent, "_StubMsg.Buffer += %u;\n", alignment);

            print_file(file, indent, "");
            write_name(file, var);
            fprintf(file, " = *(");
            write_type(file, var->type, var, var->tname);
            fprintf(file, " *)_StubMsg.Buffer;\n");
            fprintf(file, "_StubMsg.Buffer += sizeof(");
            write_type(file, var->type, var, var->tname);
            fprintf(file, ");\n");

            last_size = size;
        }
        else if (var->ptr_level == 0)
        {
            const char *ndrtype;

            switch (var->type->type)
            {
            case RPC_FC_STRUCT:
                ndrtype = "SimpleStruct";
                break;
            case RPC_FC_CSTRUCT:
            case RPC_FC_CPSTRUCT:
                ndrtype = "ConformantStruct";
                break;
            case RPC_FC_CVSTRUCT:
                ndrtype = "ConformantVaryingStruct";
                break;
            case RPC_FC_BOGUS_STRUCT:
                ndrtype = "ComplexStruct";
                break;
            default:
                error("unmarshall_arguments: Unsupported type: %s (0x%02x, ptr_level: %d)\n",
                    var->name, var->type->type, var->ptr_level);
                ndrtype = NULL;
            }

            print_file(file, indent,
                "Ndr%sUnmarshall(&_StubMsg, (unsigned char *)%s, &__MIDL_TypeFormatString.Format[%d], 0);\n",
                ndrtype, var->name, *type_offset);
            last_size = 1;
        }
        else
        {
            print_file(file, indent,
                       "NdrPointerUnmarshall(&_StubMsg, (unsigned char **)&%s, &__MIDL_TypeFormatString.Format[%d], 0);\n",
                       var->name, *type_offset);
            last_size = 1;
        }
        fprintf(file, "\n");
    }
}


size_t get_size_procformatstring_var(const var_t *var)
{
    unsigned int type_offset = 2;
    return write_procformatstring_var(NULL, 0, var, FALSE, &type_offset);
}


size_t get_size_typeformatstring_var(const var_t *var)
{
    return write_typeformatstring_var(NULL, 0, var);
}
