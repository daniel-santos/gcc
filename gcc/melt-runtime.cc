/*** file melt-runtime.cc - see http://gcc-melt.org/ for more.
     Middle End Lisp Translator [MELT] runtime support.

     Copyright (C) 2008 - 2016  Free Software Foundation, Inc.
     Contributed by Basile Starynkevitch <basile@starynkevitch.net>
       and Pierre Vittet  <piervit@pvittet.com>
       and Romain Geissler  <romain.geissler@gmail.com>
       and Jeremie Salvucci  <jeremie.salvucci@free.fr>
       and Alexandre Lissy  <alissy@mandriva.com>

     Indented with astyle.

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
along with GCC; see the file COPYING3.   If not see
<http://www.gnu.org/licenses/>.
***/

/* for debugging -fmelt-debug is useful */



#include "melt-run.h"

/* To compile MELT as a plugin, try compiling with -DMELT_IS_PLUGIN. */

#ifdef MELT_IS_PLUGIN
#include "gcc-plugin.h"
const int melt_is_plugin = 1;
#else
#include "version.h"
const int melt_is_plugin = 0;
#endif /* MELT_IS_PLUGIN */


/* since 4.7, we have a GCCPLUGIN_VERSION in plugin-version.h. */
#if defined(GCCPLUGIN_VERSION) && (GCCPLUGIN_VERSION != MELT_GCC_VERSION)
#error MELT Gcc version and GCC plugin version does not match
#if GCCPLUGIN_VERSION==5005
/** See e.g. https://lists.debian.org/debian-gcc/2015/07/msg00167.html
   and https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=793478
   or the bug report
   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66991 which is a wrong
   report, since specific to Debian.  **/

#warning some GCC 5.x installations have an incorrect plugin/include/plugin-version.h file; consider patching that file. See comment above
#endif /*GCCPLUGIN_VERSION 5005*/

#endif /*GCCPLUGIN_VERSION != MELT_GCC_VERSION */

/* the MELT branch has a BUILDING_GCC_VERSION. */
#if defined(BUILDING_GCC_VERSION) && (BUILDING_GCC_VERSION != MELT_GCC_VERSION)
#MELT Gcc version and Building Gcc version does not match
#endif /*BUILDING_GCC_VERSION != MELT_GCC_VERSION */

#if MELT_GCC_VERSION < 5000
#error this MELT is for GCC 5 or newer
#endif



/* include the definition of melt_run_preprocessed_md5 */
#include "melt-run-md5.h"

/* include a generated files of strings constants */
#include "melt-runtime-params-inc.c"


#if defined(MELT_GCC_VERSION) && (MELT_GCC_VERSION > 0)
const int melt_gcc_version = MELT_GCC_VERSION;
#else
#error should be given a MELT_GCC_VERSION
#endif

// C++ standard includes used by the MELT runtime
#include <map>
#include <exception>
#include <stdexcept>

// various system & GCC includes used by the MELT runtime:
// locale_utf8 needs:
#include "intl.h"

// opendir needs:
#include <sys/types.h>
#include <dirent.h>

// for struct walk_stmt_info
#include "gimple-walk.h"
// for print_node_brief
#include "print-tree.h"

// for print_gimple_stmt
#include "gimple-pretty-print.h"

////// the actually done modes
std::vector<std::string> melt_done_modes_vector;

///// the asked modes
std::vector<std::string> melt_asked_modes_vector;

#define MELT_MAGICSYMB_FILE "_MELT_FILE_"
#define MELT_MAGICSYMB_LINE "_MELT_LINE_"

////// builtin MELT parameter settings


/* The GC minor zone size, in kilowords, should be comparable to or
   perhaps twice as biggger as the L3 or L4 cache size on desktop
   processors. So, some megabytes i.e. thousands of kilowords. */
#define MELT_MIN_MINORSIZE_KW 512
#define MELT_DEFAULT_MINORSIZE_KW 1024
#define MELT_MAX_MINORSIZE_KW 32678

/* The full GC threshold size, in kilowords, is the allocation
   threshold for triggering the full GC.  When MELT cumulated
   allocation inside the minor zone and even after some minor GCs
   reaches that threshold, a full GC is necessarily run.  In practice,
   most MELT are temporary so don't survive minor GCs, and this
   threshold can be quite high; in principle it is nearly useless so
   could be very high.  */
#define MELT_MIN_FULLTHRESHOLD_KW 98304
#define MELT_DEFAULT_FULLTHRESHOLD_KW 524288
#define MELT_MAX_FULLTHRESHOLD_KW 4194304

/* Periodically the full GC [including Ggc] is forcibly run after a
   MELT minor GC. You don't want that to happen often, so the default
   period is quite high; in principle it could be very high. */
#define MELT_MIN_PERIODFULL 512
#define MELT_DEFAULT_PERIODFULL 4096
#define MELT_MAX_PERIODFULL 262144

////// end of MELT parameter settings




#ifndef MELT_ENTERFRAME
#error MELT runtime needs a MELT_ENTERFRAME macro
#endif


struct plugin_gcc_version *melt_plugin_gcc_version;

long melt_pass_instance_counter;
long melt_current_pass_index_var;
opt_pass* melt_current_pass_ptr;

static bool melt_verbose_full_gc;

#if defined (GCCPLUGIN_VERSION)
const int melt_gccplugin_version = GCCPLUGIN_VERSION;
#else
const int melt_gccplugin_version = 0;
#endif

int melt_count_runtime_extensions;

long melt_blocklevel_signals;

volatile sig_atomic_t melt_signaled;
volatile sig_atomic_t melt_got_sigio;
volatile sig_atomic_t melt_got_sigalrm;
volatile sig_atomic_t melt_got_sigchld;


static struct timeval melt_start_time;

#define MELT_DESC_FILESUFFIX "+meltdesc.c"
#define MELT_TIME_FILESUFFIX "+melttime.h"
#define MELT_DEFAULT_FLAVOR "optimized"

/* Our copying garbage collector needs a vector of melt_ptr_t to scan,
   a la Cheney.  We use our own "vector" of values type which does not
   depend upon vec.h .... Before svn rev 193297 of november 2012 we
   had a melt_bscanvec vector using vec.h .... */

static GTY (()) struct melt_valuevector_st* melt_scangcvect;


/* the file gt-melt-runtime.h is generated by gengtype from
   melt-runtime.c & melt-runtime.h, and from MELT generated
   melt/generated/meltrunsup.h and melt/generated/meltrunsup-inc.cc
   files.  We need to include it here, because it could define our
   ggc_alloc_* macros. */
#include "gt-melt-runtime.h"

const char melt_runtime_build_date[] = __DATE__;

int melt_debug_garbcoll;	/* Can be set in GDB, and is used by
				   melt_debuggc_eprintf!  */

static int melt_debugging_after_mode;

static long melt_forwarded_copy_byte_count;

/* the generating GGC marking routine */
extern void gt_ggc_mx_melt_un (void *);

#ifndef ggc_alloc_cleared_melt_valuevector_st
#define ggc_alloc_cleared_melt_valuevector_st(S) \
  ((struct melt_valuevector_st*) (ggc_internal_cleared_alloc ((S) MEM_STAT_INFO)))
#endif

static void
melt_resize_scangcvect (unsigned long size)
{
  gcc_assert (melt_scangcvect != NULL);
  gcc_assert (melt_scangcvect->vv_ulen <= size);
  size = ((size + 70) | 0x1f)-2;
  if (melt_scangcvect->vv_size != size)
    {
      struct melt_valuevector_st* oldgcvec = melt_scangcvect;
      unsigned long ulen = oldgcvec->vv_ulen;
      struct melt_valuevector_st* newgcvec
      = (struct melt_valuevector_st*)
          ggc_internal_cleared_alloc
          (
            (sizeof(struct melt_valuevector_st)
             +size*sizeof(melt_ptr_t))
            PASS_MEM_STAT);
      newgcvec->vv_size = size;
      newgcvec->vv_ulen = ulen;
      memcpy (newgcvec->vv_tab, oldgcvec->vv_tab,
              ulen * sizeof(melt_ptr_t));
      memset (oldgcvec, 0, sizeof(struct melt_valuevector_st)+ulen*sizeof(melt_ptr_t));
      ggc_free (oldgcvec), oldgcvec = NULL;
      melt_scangcvect = newgcvec;
    }
}



/* A nice buffer size for input or output. */
#define MELT_BUFSIZE 8192

// melt_flag_dont_catch_crashing_signals is used in toplev.c to
// disable the catching of crashing signals.
MELT_EXTERN int melt_flag_dont_catch_crashing_signals;
int melt_flag_dont_catch_crashing_signals = 0;

int melt_flag_debug = 0;
int melt_flag_bootstrapping = 0;
int melt_flag_generate_work_link = 0;
int melt_flag_keep_temporary_files = 0;

/**
   NOTE:  october 2009

   libiberty is not fully available from a plugin. So we need to
   reproduce here some functions provided in libiberty.h
**/
char *
xstrndup (const char *s, size_t n)
{
  char *result;
  size_t len = strlen (s);
  if (n < len)
    len = n;
  result = XNEWVEC (char, len + 1);
  result[len] = '\0';
  return (char *) memcpy (result, s, len);
}



/* *INDENT-OFF* */

/* we use the plugin registration facilities, so this is the plugin
   name in use */
const char* melt_plugin_name;


/* The start and end of the birth region. */
void* melt_startalz=NULL;
void* melt_endalz=NULL;
/* The current allocation pointer inside the birth region.  Move
   upwards from start to end. */
char* melt_curalz=NULL;
/* The current store pointer inside the birth region.  Move downwards
   from end to start.  */
void** melt_storalz=NULL;
/* The initial store pointer. Don't change, but should be cleared
   outside of MELT.  */
void** melt_initialstoralz = NULL;

bool melt_is_forwarding=FALSE;
long melt_forward_counter=0;

static long melt_minorsizekilow = 0;
static long melt_fullthresholdkilow = 0;
static int melt_fullperiod = 0;



/* File containing the generated C file list. Enabled by the
   -f[plugin-arg-]melt-generated-c-file-list= program option. */
static FILE* melt_generated_c_files_list_fil;

/* Debugging file for tracing dlopen & dlsym calls for
   modules. Enabled by the GCCMELT_TRACE_MODULE environment
   variable. */
static FILE* melt_trace_module_fil;
/* Debugging file for tracing source files related things. Enabled by
   the GCCMELT_TRACE_SOURCE environment variable. */
static FILE* melt_trace_source_fil;



/* The start routine of every MELT module is named
   melt_start_this_module and gets its parent environment and returns
   the newly built current environment. */
typedef melt_ptr_t (melt_start_rout_t) (melt_ptr_t);

/* The data forwarding routine of MELT modules or extensions is for
   the MELT garbage collectors, forwarding values */
typedef void melt_forwarding_rout_t (void);

/* The data marking routine of MELT modules or extensions is for Ggc */
typedef void melt_marking_rout_t (void);

#define MELT_MODULE_PLAIN_MAGIC 0x12179fe5 /*303538149*/
#define MELT_MODULE_EXTENSION_MAGIC 0x1dbe6249 /*499016265*/

class Melt_Plain_Module;
class Melt_Extension_Module;

class Melt_Module
{
  friend void melt_marking_callback (void*, void*);
  friend void melt_minor_copying_garbage_collector (size_t);
  friend void* melt_dlsym_all  (const char*);
  static std::vector<Melt_Module*> _mm_vect_;
  static std::map<std::string,Melt_Module*> _mm_map_;
  static int _mm_count_;
protected:
  unsigned _mm_magic;
  int _mm_index;		// the index in _mm_vect_;
  void * _mm_dlh;
  std::string _mm_modpath;	// dlopened module path
  std::string _mm_descrbase;	// base path of descriptive file without +meltdesc.c suffix
  melt_forwarding_rout_t *_mm_forwardrout; // forwarding routine for MELT garbage collector
  melt_marking_rout_t * _mm_markrout;	   // marking routine for Ggc
  Melt_Module (unsigned magic, const char*modpath, const char* descrbase, void*dlh=NULL);
  virtual ~Melt_Module ();
  static Melt_Module *unsafe_nth_module(int rk)
  {
    if (rk<0) rk +=  _mm_vect_.size();
    return _mm_vect_[rk];
  };
  void run_marking(void)
  {
    if (_mm_markrout)
      {
        melt_debuggc_eprintf("Melt_Module::run_marking index#%d descrbase=%s before",
                             _mm_index, _mm_descrbase.c_str());
        (*_mm_markrout) ();
        melt_debuggc_eprintf("Melt_Module::run_marking index#%d descrbase=%s after",
                             _mm_index, _mm_descrbase.c_str());
      }
  };
  void run_forwarding (void)
  {
    if (_mm_forwardrout)
      {
        melt_debuggc_eprintf("Melt_Module::run_forwarding index#%d descrbase=%s before",
                             _mm_index, _mm_descrbase.c_str());
        (*_mm_forwardrout) ();
        melt_debuggc_eprintf("Melt_Module::run_forwarding index#%d descrbase=%s after",
                             _mm_index, _mm_descrbase.c_str());
      }
  };
public:
  void *get_dlsym (const char*name) const
  {
    return (_mm_dlh && name)?dlsym(_mm_dlh,name):NULL;
  };
  bool valid_magic () const
  {
    return  _mm_magic == MELT_MODULE_PLAIN_MAGIC || _mm_magic == MELT_MODULE_EXTENSION_MAGIC;
  };
  const char* module_path () const
  {
    return _mm_modpath.c_str();
  };
  Melt_Plain_Module* as_plain_module()
  {
    if (_mm_magic == MELT_MODULE_PLAIN_MAGIC) return reinterpret_cast<Melt_Plain_Module*>(this);
    return NULL;
  };
  Melt_Extension_Module* as_extension_module()
  {
    if (_mm_magic == MELT_MODULE_EXTENSION_MAGIC) return reinterpret_cast<Melt_Extension_Module*>(this);
    return NULL;
  };
  static int nb_modules ()
  {
    return _mm_vect_.size()-1;
  };
  static void initialize();
  static Melt_Module* nth_module (int rk)
  {
    if (rk<0) rk +=  _mm_vect_.size();
    if (rk > 0 && rk < (int) _mm_vect_.size()) return _mm_vect_[rk];
    return NULL;
  };
  static Melt_Module* module_of_name (const std::string& name)
  {
    if (name.empty())
      return NULL;
    std::map<std::string,Melt_Module*>::const_iterator it = _mm_map_.find(name);
    if (it == _mm_map_.end())
      return NULL;
    return it->second;
  }
  void set_forwarding_routine (melt_forwarding_rout_t* r)
  {
    _mm_forwardrout = r;
  };
  void set_marking_routine (melt_marking_rout_t *r)
  {
    _mm_markrout = r;
  };
  int index () const
  {
    return _mm_index;
  };
};				// end class Melt_Module

// a plain module
class Melt_Plain_Module : public Melt_Module
{
  bool _pm_started;
  melt_start_rout_t *_pm_startrout;
  friend melt_ptr_t meltgc_start_module_by_index (melt_ptr_t, int);
public:
  Melt_Plain_Module (const char*modpath, const char*descrbase, void*dlh=NULL)
    : Melt_Module (MELT_MODULE_PLAIN_MAGIC, modpath, descrbase, dlh),
      _pm_started(false), _pm_startrout(NULL) {};
  virtual ~Melt_Plain_Module()
  {
    _pm_startrout=NULL;
  };
  void set_start_routine (melt_start_rout_t* r)
  {
    _pm_startrout = r;
  };
  melt_ptr_t start_it (melt_ptr_t p)
  {
    if (_pm_startrout)
      {
        _pm_started = true;
        return (*_pm_startrout) (p);
      }
    return NULL;
  };
  bool started () const
  {
    return _pm_started;
  };
};				// end class Melt_Plain_Module

// an extension module
class Melt_Extension_Module : public Melt_Module
{
  friend melt_ptr_t meltgc_run_cc_extension (melt_ptr_t, melt_ptr_t, melt_ptr_t);

  Melt_Extension_Module (const char*modpath, const char*descrbase, void*dlh=NULL)
    : Melt_Module (MELT_MODULE_EXTENSION_MAGIC, modpath, descrbase, dlh) {};
  virtual ~Melt_Extension_Module() {};
};				// end class Melt_Extension_Module

std::vector<Melt_Module*> Melt_Module::_mm_vect_;
std::map<std::string,Melt_Module*> Melt_Module::_mm_map_;
int  Melt_Module::_mm_count_;

void Melt_Module::initialize ()
{
  gcc_assert (_mm_vect_.size () == 0);
  _mm_vect_.reserve (200);
  _mm_vect_.push_back(NULL);
}

Melt_Module::Melt_Module (unsigned magic, const char*modpath, const char* descrbase, void*dlh)
  : _mm_magic(0), _mm_index(0), _mm_dlh(NULL), _mm_modpath(), _mm_descrbase(),
    _mm_forwardrout(NULL), _mm_markrout(NULL)
{
  int ix = _mm_vect_.size();
  gcc_assert (ix > 0);
  gcc_assert (magic ==  MELT_MODULE_PLAIN_MAGIC || magic == MELT_MODULE_EXTENSION_MAGIC);
  gcc_assert (modpath != NULL && modpath[0] != (char)0);
  gcc_assert (descrbase != NULL && descrbase[0] != (char)0);
  if (access(modpath, R_OK))
    melt_fatal_error ("cannot make module of path %s - %s", modpath, xstrerror(errno));
  _mm_modpath = std::string(modpath);
  _mm_descrbase = std::string(descrbase);
  if (!dlh)
    {
      errno = 0;
      dlh = dlopen (_mm_modpath.c_str(),  RTLD_NOW | RTLD_GLOBAL);
      if (!dlh)
        {
          static char dldup[256];
          const char*dle = dlerror();
          if (!dle) dle = "??";
          strncpy (dldup, dle, sizeof(dldup)-1);
          melt_fatal_error ("failed to dlopen Melt module %s - %s", _mm_modpath.c_str(), dldup);
        }
    }
  _mm_dlh = dlh;
  _mm_index = ix;
  _mm_magic = magic;
  gcc_assert (_mm_magic ==  MELT_MODULE_PLAIN_MAGIC || _mm_magic == MELT_MODULE_EXTENSION_MAGIC);
  _mm_vect_.push_back(this);
  _mm_count_++;
  std::string modbasename = basename(descrbase);
  _mm_map_[modbasename] = this;
}

Melt_Module::~Melt_Module()
{
  gcc_assert (_mm_index > 0);
  gcc_assert (_mm_magic ==  MELT_MODULE_PLAIN_MAGIC || _mm_magic == MELT_MODULE_EXTENSION_MAGIC);
  gcc_assert (_mm_vect_.at(_mm_index) == this);
  gcc_assert (!_mm_modpath.empty());
  _mm_forwardrout = NULL;
  _mm_markrout = NULL;
  if (_mm_dlh)
    {
      if (dlclose (_mm_dlh))
        melt_fatal_error ("failed to dlclose module #%d %s - %s",
                          _mm_index, _mm_modpath.c_str(), dlerror());
      _mm_dlh = 0;
    }
  _mm_vect_[_mm_index] = NULL;
  std::string modbasename = basename(_mm_descrbase.c_str());
  _mm_map_.erase (modbasename);
  _mm_count_--;
}





Melt_CallProtoFrame* melt_top_call_frame =NULL;
FILE* Melt_CallProtoFrame::_dbgcall_file_ = NULL;
long Melt_CallProtoFrame::_dbgcall_count_ = 0L;

/* The start routine of every MELT extension (dynamically loaded
   shared object to evaluate at runtime some expressions in a given
   environment, e.g. used for read-eval-print-loop etc...) is named
   melt_start_run_extension, its parameters are a box containing the
   current environment to be extended and a tuple for literal
   values. It is returning the resulting value of the evaluation.
 */
typedef melt_ptr_t melt_start_runext_rout_t (melt_ptr_t /*boxcurenv*/, melt_ptr_t /*tuplitval*/);

/** special values are linked in a list to permit their explicit
deletion */

struct meltspecialdata_st* melt_newspecdatalist;
struct meltspecialdata_st* melt_oldspecdatalist;

/* number of kilowords allocated in the your heap since last full MELT
   garbage collection */
unsigned long melt_kilowords_sincefull;
/* cumulated kilowords forwarded & copied to old Ggc heap */
unsigned long melt_kilowords_forwarded;
/* number of full & any melt garbage collections */
unsigned long melt_nb_full_garbcoll;
unsigned long melt_nb_garbcoll;

/* counting various reasons for full garbage collection: */
unsigned long melt_nb_fullgc_because_asked;
unsigned long melt_nb_fullgc_because_periodic;
unsigned long melt_nb_fullgc_because_threshold;
unsigned long melt_nb_fullgc_because_copied;

void* melt_touched_cache[MELT_TOUCHED_CACHE_SIZE];
bool melt_prohibit_garbcoll;

long melt_dbgcounter;
long melt_debugskipcount;

long melt_error_counter;

/* Forward declaration, because meltrunsup-inc.cc uses it. */
static void melt_resize_scangcvect (unsigned long size);

/* File meltrunsup-inc.cc is inside melt/generated/ */
#include "meltrunsup-inc.cc"

/* an strdup-ed version string of gcc */
char* melt_gccversionstr;

int melt_last_global_ix = MELTGLOB__LASTGLOB;
melt_ptr_t melt_globalptrs[MELT_NB_GLOBALS];
bool melt_touchedglobalchunk[MELT_NB_GLOBAL_CHUNKS];

static void* proghandle;


/* to code case ALL_MELTOBMAG_SPECIAL_CASES: */
#define ALL_MELTOBMAG_SPECIAL_CASES             \
         MELTOBMAG_SPEC_FILE:                   \
    case MELTOBMAG_SPEC_RAWFILE

/* Obstack used for reading names */
static struct obstack melt_bname_obstack;

const char* melt_version_str (void)
{
#ifndef MELT_REVISION
#error MELT_REVISION not defined at command line compilation
#endif
  /* MELT_REVISION is always a preprocessor constant string, because
     this file is compiled with something like
     -DMELT_REVISION='"foobar"' .... */
  return MELT_VERSION_STRING  " "  MELT_REVISION;
}

/******************************************************************/
/* Interned C strings. */
static unsigned melt_cstring_hash (const char*ps)
{
  unsigned h = 0;
  const unsigned char*s = (const unsigned char*)ps;
  if (!s) return 0;
  for (;;)
    {
      if (!s[0] || !s[1] || !s[2] || !s[3])
        break;
      h = (29*h) ^ ((s[0]*41) + (s[1]*23));
      h = (17*h) - ((s[2]*71) ^ (s[3]*53));
      h &= 0x3fffffff;
      s += 4;
    }
  if (s[0]) h ^= (s[0]*13);
  if (s[1]) h = (11*h) + (s[1]*79);
  if (s[2]) h = (83*h) ^ (s[2]*47);
  if (s[3]) h = (29*h) - (s[3]*97);
  h &= 0x3fffffff;
  return h;
}

static struct
{
  unsigned char csh_sizix; /* allocated size index inside melt_primtab */
  unsigned csh_count;		/* used count */
  const char** csh_array;	/* of melt_primtab[csh_sizix] pointers */
} melt_intstrhtab;


static long
melt_raw_interned_cstring_index (const char*s)
{
  unsigned h = 0;
  unsigned ix = 0;
  unsigned m = 0;
  unsigned long siz = melt_primtab[melt_intstrhtab.csh_sizix];
  gcc_assert (melt_intstrhtab.csh_count + 5 < siz);
  gcc_assert (s != NULL);
  gcc_assert (melt_intstrhtab.csh_array != NULL);
  h = melt_cstring_hash(s);
  m = h % siz;
  for (ix = m; ix < siz; ix++)
    {
      const char*curs = melt_intstrhtab.csh_array[ix];
      if (!curs)
        {
          melt_intstrhtab.csh_array[ix] = s;
          melt_intstrhtab.csh_count++;
          return ix;
        }
      else if (!strcmp(s, curs))
        return ix;
    }
  for (ix = 0; ix < m; ix++)
    {
      const char*curs = melt_intstrhtab.csh_array[ix];
      if (!curs)
        {
          melt_intstrhtab.csh_array[ix] = s;
          melt_intstrhtab.csh_count++;
          return ix;
        }
      else if (!strcmp(s, curs))
        return ix;
    }
  return -1;
}


const char*
melt_intern_cstring (const char* s)
{
  if (!s) return NULL;
  /* This test is also true when melt_intstrhtab is initially cleared! */
  if (MELT_UNLIKELY (4*melt_intstrhtab.csh_count + 50
                     > 3*melt_primtab[melt_intstrhtab.csh_sizix]))
    {
      unsigned oldsiz = melt_primtab[melt_intstrhtab.csh_sizix];
      unsigned oldcount =melt_intstrhtab.csh_count;
      unsigned newix = 0;
      unsigned newsiz = 0;
      char**newarr = NULL;
      const char**oldarr = NULL;
      unsigned ix=0;
      unsigned oix=0;
      for (ix = 1;
           ix < sizeof(melt_primtab)/sizeof(melt_primtab[0]) && newix==0;
           ix++)
        if (melt_primtab[ix] > 2*melt_intstrhtab.csh_count + 60)
          newix = ix;
      if (!newix)
        /* should really never happen... */
        melt_fatal_error ("MELT interned string hash table overflow %u for %s",
                          melt_intstrhtab.csh_count, s);
      newsiz = melt_primtab[newix];
      gcc_assert (newsiz > melt_intstrhtab.csh_count + 10);
      gcc_assert (newsiz > oldsiz);
      newarr = (char**) xcalloc (newsiz, sizeof(char*));
      oldarr = melt_intstrhtab.csh_array;
      melt_intstrhtab.csh_count = 0;
      melt_intstrhtab.csh_sizix = newix;
      melt_intstrhtab.csh_array = (const char**) CONST_CAST (char**, newarr);
      for (oix=0; oix < oldsiz; oix++)
        {
          long aix = -1;
          const char*curs = oldarr[oix];
          if (!curs)
            continue;
          aix = melt_raw_interned_cstring_index (curs);
          gcc_assert (aix >= 0);
        };
      gcc_assert (melt_intstrhtab.csh_count == oldcount);
    };
  {
    long j = melt_raw_interned_cstring_index (s);
    const char* ns = NULL;
    gcc_assert (j >= 0 && j < melt_primtab[melt_intstrhtab.csh_sizix]);
    ns = melt_intstrhtab.csh_array[j];
    gcc_assert (ns != NULL);
    if (ns == s)
      melt_intstrhtab.csh_array[j] = ns = xstrdup (s);
    return ns;
  }
}

/*****************************************************************/
#if MELT_HAVE_RUNTIME_DEBUG > 0

void melt_break_alptr_1_at (const char*msg, const char* fil, int line);
void melt_break_alptr_2_at (const char*msg, const char* fil, int line);
#define melt_break_alptr_1(Msg) melt_break_alptr_1_at((Msg),__FILE__,__LINE__)
#define melt_break_alptr_2(Msg) melt_break_alptr_2_at((Msg),__FILE__,__LINE__)

void
melt_break_alptr_1_at (const char*msg, const char* fil, int line)
{
  fprintf (stderr, "melt_break_alptr_1 %s:%d: %s alptr_1=%p\n",
           melt_basename(fil), line, msg, melt_alptr_1);
  fflush (stderr);
}

void
melt_break_alptr_2_at (const char*msg, const char* fil, int line)
{
  fprintf (stderr, "melt_break_alptr_2 %s:%d: %s alptr_2=%p\n",
           melt_basename(fil), line, msg, melt_alptr_2);
  fflush (stderr);
}

void melt_break_objhash_1_at (const char*msg, const char* fil, int line);
void melt_break_objhash_2_at (const char*msg, const char* fil, int line);
#define melt_break_objhash_1(Msg) melt_break_objhash_1_at((Msg),__FILE__,__LINE__)
#define melt_break_objhash_2(Msg) melt_break_objhash_2_at((Msg),__FILE__,__LINE__)

void
melt_break_objhash_1_at (const char*msg, const char* fil, int line)
{
  fprintf (stderr, "melt_break_objhash_1 %s:%d: %s objhash_1=%#x\n",
           melt_basename(fil), line, msg, melt_objhash_1);
  fflush (stderr);
}

void
melt_break_objhash_2_at (const char*msg, const char* fil, int line)
{
  fprintf (stderr, "melt_break_objhash_2 %s:%d: %s objhash_2=%#x\n",
           melt_basename(fil), line, msg, melt_objhash_2);
  fflush (stderr);
}

#endif /*MELT_HAVE_DEBUG*/

/* The allocation & freeing of the young zone is a routine, for ease
   of debugging. */
static void
melt_allocate_young_gc_zone (long wantedbytes)
{
  if (wantedbytes & 0x3fff)
    wantedbytes = (wantedbytes | 0x3fff) + 1;
  melt_debuggc_eprintf("allocate #%ld young zone %ld [=%ldK] bytes",
                       melt_nb_garbcoll, wantedbytes, wantedbytes >> 10);
  melt_startalz = melt_curalz =
                    (char *) xcalloc (wantedbytes / sizeof (void *),
                                      sizeof(void*));
  melt_endalz = (char *) melt_curalz + wantedbytes;
  melt_storalz = melt_initialstoralz = ((void **) melt_endalz) - 2;
  melt_debuggc_eprintf("allocated young zone %p-%p",
                       (void*)melt_startalz, (void*)melt_endalz);
  /* You could put a breakpoint here under gdb! */
  gcc_assert (melt_startalz != NULL);
#if MELT_HAVE_RUNTIME_DEBUG>0
  if (MELT_UNLIKELY(melt_alptr_1 != NULL
                    && (char*)melt_alptr_1 >= (char*)melt_startalz
                    && (char*)melt_alptr_1 < (char*)melt_endalz))
    {
      fprintf (stderr, "melt_allocate_young_gc_zone zone %p-%p with alptr_1 %p;",
               (void*)melt_startalz,  (void*)melt_endalz, melt_alptr_1);
      fflush (stderr);
      melt_debuggc_eprintf("allocate #%ld young with alptr_1 %p;",
                           melt_nb_garbcoll, melt_alptr_1);
      melt_break_alptr_1 ("allocate with alptr_1");
    };
  if (MELT_UNLIKELY(melt_alptr_2 != NULL
                    && (char*)melt_alptr_2 >= (char*)melt_startalz
                    && (char*)melt_alptr_2 < (char*)melt_endalz))
    {
      fprintf (stderr, "melt_allocate_young_gc_zone zone %p-%p with alptr_2 %p;",
               (void*)melt_startalz,  (void*)melt_endalz, melt_alptr_2);
      fflush (stderr);
      melt_debuggc_eprintf("allocate #%ld young with alptr_2 %p;",
                           melt_nb_garbcoll, melt_alptr_2);
      melt_break_alptr_2 ("allocate with alptr_2");
    };
#endif /*MELT_HAVE_DEBUG*/
  return;
}

static void
melt_free_young_gc_zone (void)
{
  gcc_assert (melt_startalz != NULL);
  melt_debuggc_eprintf("freeing #%ld young zone %p-%p",
                       melt_nb_garbcoll,
                       (void*)melt_startalz, (void*)melt_endalz);
#if MELT_HAVE_RUNTIME_DEBUG>0
  if (MELT_UNLIKELY(melt_alptr_1 && (char*)melt_alptr_1 >= (char*)melt_startalz
                    && (char*)melt_alptr_1 < (char*)melt_endalz))
    {
      fprintf (stderr, "melt_free_young_gc_zone zone %p-%p with alptr_1 %p;",
               (void*)melt_startalz,  (void*)melt_endalz, melt_alptr_1);
      fflush (stderr);
      melt_debuggc_eprintf("free #%ld young with alptr_1 %p;",
                           melt_nb_garbcoll, melt_alptr_1);
      melt_break_alptr_1 ("free with alptr_1");
    };
  if (MELT_UNLIKELY(melt_alptr_2 && (char*)melt_alptr_2 >= (char*)melt_startalz
                    && (char*)melt_alptr_2 < (char*)melt_endalz))
    {
      fprintf (stderr, "melt_free_young_gc_zone zone %p-%p with alptr_2 %p;",
               (void*)melt_startalz,  (void*)melt_endalz, melt_alptr_2);
      fflush (stderr);
      melt_debuggc_eprintf("free #%ld young with alptr_2 %p;",
                           melt_nb_garbcoll, melt_alptr_2);
      melt_break_alptr_2("free with alptr_2");
    };
#endif /*MELT_HAVE_DEBUG*/
  free (melt_startalz);
  melt_startalz = melt_endalz = melt_curalz = NULL;
  melt_storalz = melt_initialstoralz = NULL;
  /* You can put a gdb breakpoint here! */
  gcc_assert (melt_nb_garbcoll > 0);
  return;
}


/* called from toplev.c function print_version */
void
melt_print_version_info (FILE *fil, const char* indent)
{
  if (!fil) return;
  if (!indent) indent="\t";
  fprintf (fil, "%sMELT built-in source directory: %s\n",
           indent, melt_source_dir);
  fprintf (fil, "%sMELT built-in module directory: %s\n",
           indent, melt_module_dir);
  if (melt_is_plugin)
    {
      fprintf (fil, "%sUse -fplugin-arg-melt-source-path= or -fplugin-arg-melt-module-path= to override them with a colon-separated path.\n",
               indent);
      fprintf (fil, "%sMELT built-in module make command [-fplugin-arg-melt-module-make-command=] %s\n",
               indent, melt_module_make_command);
      fprintf (fil, "%sMELT built-in module makefile [-fplugin-arg-melt-module-makefile=] %s\n",
               indent, melt_module_makefile);
      fprintf (fil, "%sMELT built-in module cflags [-fplugin-arg-melt-module-cflags=] %s\n",
               indent, melt_module_cflags);
      fprintf (fil, "%sMELT built-in default module list [-fplugin-arg-melt-init=@]%s\n",
               indent, melt_default_modlis);
    }
  else
    {
      fprintf (fil, "%sUse -fmelt-source-path= or -fmelt-module-path= to override them with a colon-separated path.\n",
               indent);
      fprintf (fil, "%sMELT built-in module make command [-fmelt-module-make-command=] %s\n",
               indent, melt_module_make_command);
      fprintf (fil, "%sMELT built-in module makefile [-fmelt-module-makefile=] %s\n",
               indent, melt_module_makefile);
      fprintf (fil, "%sMELT built-in module cflags [-fmelt-module-cflags=] %s\n",
               indent, melt_module_cflags);
      fprintf (fil, "%sMELT built-in default module list [-fmelt-init=@]%s\n",
               indent, melt_default_modlis);
    }
  fflush (fil);
}


/* retrieve a MELT related program or plugin argument */
#ifdef MELT_IS_PLUGIN
static int melt_plugin_argc;
static struct plugin_argument* melt_plugin_argv;

const char*
melt_argument (const char* argname)
{
  int argix=0;
  if (!argname || !argname[0])
    return NULL;
  for (argix = 0; argix < melt_plugin_argc; argix ++)
    {
      if (!strcmp(argname, melt_plugin_argv[argix].key))
        {
          char* val = melt_plugin_argv[argix].value;
          /* never return NULL if the argument is found; return an
             empty string if no value given */
          if (!val)
            return "";
          return val;
        }
    }
  return NULL;
}

#else /*!MELT_IS_PLUGIN*/

static std::map<std::string,std::string> melt_branch_argument_map;

// Function called from toplev.c on the MELT branch to process the
// MELT specific arguments (like -fplugin=melt and -fplugin-arg-melt-*
// and -fmelt-* ...)
extern "C" int melt_branch_process_arguments (int *, char***);

int
melt_branch_process_arguments (int *argcp, char***argvp)
{
  int ret=0;
  std::vector<char*> argvec;
  int oldargc = *argcp;
  char** oldargv = *argvp;
  gcc_assert (oldargc>0 && oldargv && oldargv[oldargc]==NULL);
  argvec.reserve (oldargc);
  argvec.push_back (oldargv[0]);
  for (int ix=1; ix<oldargc; ix++)
    {
      char* curarg = oldargv[ix];
      if (!curarg)
        break;
      if (!strcmp(curarg, "-fplugin=melt"))
        {
          ret++;
          inform (UNKNOWN_LOCATION, "MELT branch won't load its plugin with -fplugin=melt");
          continue;
        }
      char* meltargstart = NULL;
#define MELT_ARG_START "-fmelt-"
#define MELT_ALT_ARG_START "-fMELT-"
#define MELT_PLUGIN_ARG_START "-fplugin-arg-melt-"
      if (!strncmp (curarg, MELT_ARG_START, sizeof(MELT_ARG_START)-1)
          && curarg[sizeof(MELT_ARG_START)] != '\0')
        meltargstart = curarg+sizeof(MELT_ARG_START)-1;
      else if (!strncmp (curarg, MELT_ALT_ARG_START, sizeof(MELT_ALT_ARG_START)-1)
               && curarg[sizeof(MELT_ALT_ARG_START)] != '\0')
        meltargstart = curarg+sizeof(MELT_ALT_ARG_START)-1;
      else if (!strncmp (curarg, MELT_PLUGIN_ARG_START, sizeof(MELT_PLUGIN_ARG_START)-1)
               && curarg[sizeof(MELT_PLUGIN_ARG_START)] != '\0')
        meltargstart = curarg+sizeof(MELT_PLUGIN_ARG_START)-1;
      if (meltargstart && meltargstart[0])
        {
          std::string meltargname, meltargval;
          ret++;
          char* eq = strchr (meltargstart+1, '=');
          if (eq)
            {
              meltargname.assign (meltargstart, eq-meltargstart);
              meltargval.assign (eq+1);
            }
          else
            meltargname.assign(meltargstart);
          if (melt_branch_argument_map.find (meltargname)
              != melt_branch_argument_map.end ())
            fatal_error (UNKNOWN_LOCATION,
                         "MELT branch argument -f[plugin-arg-]melt-%s given twice '%s' and '%s'",
                         meltargname.c_str(), meltargval.c_str(),
                         melt_branch_argument_map[meltargname].c_str());
          melt_branch_argument_map [meltargname] = meltargval;
        }
      else
        argvec.push_back (curarg);
    }
  int argsize = (int) argvec.size();
  gcc_assert (argsize <= oldargc && argsize>0);
  for (int ix=1; ix<argsize; ix++)
    (*argvp)[ix] = argvec[ix];
  (*argvp)[argsize] = NULL;
  *argcp = argsize;
  {
    const char* dbgarg = melt_argument("debugging");
    if (dbgarg && !strcmp(dbgarg, "all"))
      melt_flag_debug=1;
  }
  {
    const char* catarg = melt_argument ("dont-catch-signals");
    if (catarg)
      melt_flag_dont_catch_crashing_signals = 1;
  }
  return ret;
}

/* builtin MELT, retrieve the MELT relevant program argument */
const char*
melt_argument (const char* argname)
{
  if (!argname || !argname[0])
    return NULL;
  {
    std::string argstr = argname;
    if (melt_branch_argument_map.find(argstr) != melt_branch_argument_map.end())
      return melt_branch_argument_map[argstr].c_str();
  }
  return NULL;
}

#endif /*MELT_IS_PLUGIN*/

#if defined(__GNUC__) && __GNUC__>3 /* condition to have pragma GCC poison */

#pragma GCC poison melt_mode_string


#pragma GCC poison melt_argument_string

#pragma GCC poison melt_arglist_string

/* don't poison melt_flag_debug or melt_flag_bootstrapping */
#pragma GCC poison melt_compile_script_string

#pragma GCC poison melt_count_debugskip_string

#pragma GCC poison melt_dynmodpath_string


#pragma GCC poison melt_srcpath_string

#pragma GCC poison melt_init_string

#pragma GCC poison melt_extra_string

#pragma GCC poison melt_secondargument_string

#pragma GCC poison melt_tempdir_string

#endif /* GCC >= 3 */

/* the debug depth for MELT debug_msg .... */
int melt_debug_depth (void)
{
#define MELT_DEFAULT_DEBUG_DEPTH 9
#define MELT_MINIMAL_DEBUG_DEPTH 2
#define MELT_MAXIMAL_DEBUG_DEPTH 28
  static int d;
  if (MELT_UNLIKELY(!d))
    {
      const char* dbgdepthstr = melt_argument ("debug-depth");
      d = dbgdepthstr?(atoi (dbgdepthstr)):0;
      if (d == 0)
        {
          d = MELT_DEFAULT_DEBUG_DEPTH;
          warning (0,
                   "MELT debug depth -f[plugin-arg-]melt-debug-depth= defaulted to %d",
                   d);
        }
      if (d < MELT_MINIMAL_DEBUG_DEPTH)
        {
          warning (0,
                   "MELT debug depth -f[plugin-arg-]melt-debug-depth= increased to %d but was %d ",
                   MELT_MINIMAL_DEBUG_DEPTH, d);
          d = MELT_MINIMAL_DEBUG_DEPTH;
        }
      else if (d > MELT_MAXIMAL_DEBUG_DEPTH)
        {
          warning (0,
                   "MELT debug depth -f[plugin-arg-]melt-debug-depth= decreased to %d but was %d ", MELT_MAXIMAL_DEBUG_DEPTH, d);
          d = MELT_MAXIMAL_DEBUG_DEPTH;
        }
    }
  return d;
}


#define MELTPYD_MAX_RANK 512
/* FIXME: should use a vector */
static struct melt_payload_descriptor_st* meltpyd_array[MELTPYD_MAX_RANK];



static inline void
melt_delete_specialdata (struct meltspecialdata_st *msd)
{
  unsigned kind = msd->meltspec_kind;
  struct melt_payload_descriptor_st* mpyd = NULL;
  if (kind != 0)
    {
      if (kind >= MELTPYD_MAX_RANK
          || (mpyd = meltpyd_array[kind]) == NULL)
        melt_fatal_error ("invalid kind %d of deleted special data @%p",
                          kind, (void*)msd);
      if (mpyd->meltpyd_magic != MELT_PAYLOAD_DESCRIPTOR_MAGIC
          || (mpyd->meltpyd_rank > 0 && mpyd->meltpyd_rank != kind)
          || !mpyd->meltpyd_name)
        melt_fatal_error ("invalid payload descriptor of kind %d for deleted special data @%p",
                          kind, (void*)msd);
      if (mpyd->meltpyd_destroy_rout)
        {
          melt_debuggc_eprintf ("delete_special destroying kind %d=%s data @%p",
                                kind, mpyd->meltpyd_name, (void*)msd);
          (*mpyd->meltpyd_destroy_rout) (msd, mpyd);
          melt_debuggc_eprintf ("delete_special destroyed kind %d=%s data @%p",
                                kind, mpyd->meltpyd_name, (void*)msd);
        };
    }
  memset (msd, 0, sizeof(struct meltspecialdata_st));
}


#ifdef MELT_HAVE_DEBUG
/* only for debugging, to be set from the debugger */

void *melt_checkedp_ptr1;
void *melt_checkedp_ptr2;
#endif /*MELT_HAVE_DEBUG */



static void melt_scanning (melt_ptr_t);



#if MELT_HAVE_RUNTIME_DEBUG > 0
/***
 * check our call frames
 ***/
static inline void
check_pointer_at (const char msg[], long count, melt_ptr_t * pptr,
                  const char *filenam, int lineno)
{
  unsigned magic = 0;
  melt_ptr_t ptr = *pptr;
  if (!ptr)
    return;
  if (!ptr->u_discr)
    melt_fatal_error
    ("<%s#%ld> corrupted pointer %p (at %p) without discr at %s:%d", msg,
     count, (void *) ptr, (void *) pptr, melt_basename (filenam), lineno);
  magic = ptr->u_discr->meltobj_magic;
  if (magic < MELTOBMAG__FIRST || magic >= MELTOBMAG__LAST)
    melt_fatal_error ("<%s#%ld> bad pointer %p (at %p) bad magic %d at %s:%d",
                      msg, count, (void *) ptr, (void *) pptr,
                      (int) ptr->u_discr->meltobj_magic, melt_basename (filenam),
                      lineno);
}



void
melt_caught_assign_at (void *ptr, const char *fil, int lin,
                       const char *msg)
{
  melt_debugeprintf ("caught assign %p at %s:%d /// %s", ptr, melt_basename (fil), lin,
                     msg);
}


#endif /*MELT_HAVE_RUNTIME_DEBUG*/

static unsigned long melt_nbcbreak;

void
melt_cbreak_at (const char *msg, const char *fil, int lin)
{
  melt_nbcbreak++;
  melt_debugeprintf_raw ("%s:%d: CBREAK#%ld %s\n", melt_basename (fil), lin,
			 melt_nbcbreak,
                         msg);
  gcc_assert (melt_nbcbreak>0);  // useless, but you can put a GDB breakpoint here
}

/* make a special value; return NULL if the discriminant is not special */
struct meltspecial_st*
meltgc_make_special (melt_ptr_t discr_p)
{
  unsigned magic = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define specv      meltfram__.mcfr_varptr[1]
#define sp_specv ((struct meltspecial_st*)(specv))
#define spda_specv ((struct meltspecialdata_st*)(specv))
  discrv = discr_p;
  if (!discrv || melt_magic_discr((melt_ptr_t)discrv) != MELTOBMAG_OBJECT)
    goto end;
  magic = ((meltobject_ptr_t)discrv)->meltobj_magic;
  switch (magic)
    {
    /* our new special data */
    case MELTOBMAG_SPECIAL_DATA:
    {
      specv = (melt_ptr_t) meltgc_allocate (sizeof(struct meltspecialdata_st), 0);
      memset (specv, 0, sizeof(struct meltspecialdata_st));
      spda_specv->discr = (meltobject_ptr_t) discrv;
      spda_specv->meltspec_mark = 0;
      spda_specv->meltspec_next = melt_newspecdatalist;
      melt_newspecdatalist = (struct meltspecialdata_st*)specv;
      melt_debuggc_eprintf ("make_special data %p discr %p magic %d %s",
                            (void*)specv, (void*)discrv, magic, melt_obmag_string(magic));
#if MELT_HAVE_RUNTIME_DEBUG>0
      if (melt_alptr_1 && (void*)melt_alptr_1 == specv)
        {
          fprintf (stderr, "meltgc_make_special data alptr_1 %p mag %d %s\n",
                   melt_alptr_1, magic, melt_obmag_string(magic));
          fflush (stderr);
          melt_break_alptr_1 ("meltgc_make_special data alptr_1");
        };
      if (melt_alptr_2 && (void*)melt_alptr_2 == specv)
        {
          fprintf (stderr, "meltgc_make_special data alptr_2 %p mag %d %s\n",
                   melt_alptr_2, magic, melt_obmag_string(magic));
          fflush (stderr);
          melt_break_alptr_2 ("meltgc_make_special data alptr_2");
        };
#endif /*MELT_HAVE_DEBUG*/
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME();
  return sp_specv;
#undef discrv
#undef specv
#undef sp_specv
#undef spda_specv
}


/* make a special value; return NULL if the discriminant is not special data */
struct meltspecialdata_st*
meltgc_make_specialdata (melt_ptr_t discr_p)
{
  unsigned magic = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define specv      meltfram__.mcfr_varptr[1]
#define spda_specv ((struct meltspecialdata_st*)(specv))
  discrv = discr_p;
  if (!discrv || melt_magic_discr((melt_ptr_t)discrv) != MELTOBMAG_OBJECT)
    goto end;
  magic = ((meltobject_ptr_t)discrv)->meltobj_magic;
  if (magic != MELTOBMAG_SPECIAL_DATA)
    goto end;
  specv = (melt_ptr_t) meltgc_allocate (sizeof(struct meltspecialdata_st), 0);
  memset (specv, 0, sizeof(struct meltspecialdata_st));
  spda_specv->discr = (meltobject_ptr_t) discrv;
  spda_specv->meltspec_mark = 0;
  spda_specv->meltspec_next = melt_newspecdatalist;
  melt_newspecdatalist = (struct meltspecialdata_st*)specv;
  melt_debuggc_eprintf ("make_specialdata %p discr %p magic %d %s",
                        (void*)specv, (void*)discrv, magic, melt_obmag_string(magic));
#if MELT_HAVE_RUNTIME_DEBUG>0
  if (melt_alptr_1 && (void*)melt_alptr_1 == specv)
    {
      fprintf (stderr, "meltgc_make_specialdata alptr_1 %p mag %d %s\n",
               melt_alptr_1, magic, melt_obmag_string(magic));
      fflush (stderr);
      melt_break_alptr_1 ("meltgc_make_special data alptr_1");
    };
  if (melt_alptr_2 && (void*)melt_alptr_2 == specv)
    {
      fprintf (stderr, "meltgc_make_specialdata alptr_2 %p mag %d %s\n",
               melt_alptr_2, magic, melt_obmag_string(magic));
      fflush (stderr);
      melt_break_alptr_2 ("meltgc_make_specialdata alptr_2");
    };
#endif /*MELT_HAVE_DEBUG*/
end:
  MELT_EXITFRAME();
  return spda_specv;
#undef discrv
#undef specv
#undef spda_specv
}



char*
meltgc_specialdata_sprint (melt_ptr_t specd_p)
{
  char *res = NULL;
  unsigned kind = 0;
  struct melt_payload_descriptor_st* mpyd = NULL;
  MELT_ENTERFRAME (1, NULL);
#define specv  meltfram__.mcfr_varptr[0]
#define spda_specv ((struct meltspecialdata_st*)(specv))
  specv = specd_p;
  if (melt_magic_discr ((melt_ptr_t) specv) != MELTOBMAG_SPECIAL_DATA)
    goto end;
  kind = spda_specv->meltspec_kind;
  if (kind > 0 && kind < MELTPYD_MAX_RANK && (mpyd = meltpyd_array[kind]) != NULL)
    {
      gcc_assert (mpyd->meltpyd_magic == MELT_PAYLOAD_DESCRIPTOR_MAGIC);
      gcc_assert (mpyd->meltpyd_rank == 0 || mpyd->meltpyd_rank == kind);
      if (mpyd->meltpyd_sprint_rout)
        res = (*mpyd->meltpyd_sprint_rout) (spda_specv, mpyd);
      if (!res)
        {
          char resbuf[80];
          snprintf (resbuf, sizeof(resbuf), "special:%s@@%p.%#lx",
                    mpyd->meltpyd_name, (void*) specv,
                    (long) spda_specv->meltspec_payload.meltpayload_ptr1);
          res = xstrdup (resbuf);
        }
    }
end:
  MELT_EXITFRAME ();
  return res;
#undef specv
#undef spda_specv
}


/***
 * the marking routine is registered thru PLUGIN_GGC_MARKING
 * it makes GGC play nice with MELT.
 **/
static long meltmarkingcount;


void
melt_marking_callback (void *gcc_data ATTRIBUTE_UNUSED,
                       void* user_data ATTRIBUTE_UNUSED)
{
  int ix = 0;
  melt_ptr_t *storp = NULL;
  meltmarkingcount++;
  dbgprintf ("start of melt_marking_callback %ld", meltmarkingcount);
  /* Call the marking of every plain and extension modules */
  int nbmod = Melt_Module::nb_modules();
  for (ix = 1; ix <= nbmod; ix++)
    {
      Melt_Module* cmod = Melt_Module::unsafe_nth_module (ix);
      gcc_assert (cmod != NULL && cmod->valid_magic());
      cmod->run_marking ();
    }
  ///////
  for (Melt_CallFrame *mcf = (Melt_CallFrame*)melt_top_call_frame;
       mcf != NULL;
       mcf = (Melt_CallFrame*)mcf->_meltcf_prev)
    {
      mcf->melt_mark_ggc_data ();
      if (mcf->current())
        gt_ggc_mx_melt_un (mcf->current());
    }
  //////
  /* mark the store list.  */
  if (melt_storalz)
    for (storp = (melt_ptr_t *) melt_storalz;
         (char *) storp < (char *) melt_endalz; storp++)
      {
        melt_ptr_t curstorp = (melt_ptr_t) *storp;
        if (curstorp)
          gt_ggc_mx_melt_un (curstorp);
      }
  dbgprintf("end of melt_marking_callback %ld", meltmarkingcount);
}


static void
melt_delete_unmarked_new_specialdata (void)
{
  struct meltspecialdata_st *specda = NULL;
  /* Delete every unmarked special data on the new list and clear it */
  for (specda = melt_newspecdatalist; specda != NULL; specda = specda->meltspec_next)
    {
      gcc_assert (melt_is_young (specda));
      melt_debuggc_eprintf ("melt_delete_unmarked_new_specialdata specda %p has mark %d",
                            (void*) specda, specda->meltspec_mark);

#if MELT_HAVE_RUNTIME_DEBUG > 0
      if (melt_alptr_1 && (void*)melt_alptr_1 == (void*)specda)
        {
          unsigned mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_delete_unmarked_new_specialdata new special alptr_1 %p mag %d\n",  melt_alptr_1, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_delete_unmarked_new_specialdata #%ld new special alptr_1 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_1, mag);
          melt_break_alptr_1 ("garbcoll new specialdata alptr_1");
        }
      if (melt_alptr_2 && (void*)melt_alptr_2 == (void*)specda)
        {
          unsigned mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_delete_unmarked_new_specialdata new special alptr_2 %p mag %d\n",  melt_alptr_2, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_delete_unmarked_new_specialdata #%ld new special alptr_2 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_2, mag);
          melt_break_alptr_2 ("garbcoll new specialdata alptr_2");
        }
#endif /*MELT_HAVE_DEBUG*/

      if (!specda->meltspec_mark)
        {
          melt_debuggc_eprintf ("melt_delete_unmarked_new_specialdata deleting newspec %p", (void*)specda);
          melt_delete_specialdata (specda);
        }
    }
  melt_newspecdatalist = NULL;
}


/* The minor MELT GC is a copying generational garbage collector whose
   old space is the GGC heap.  */
void
melt_minor_copying_garbage_collector (size_t wanted)
{
  melt_ptr_t *storp = NULL;
  int ix = 0;
  melt_check_call_frames (MELT_ANYWHERE, "before garbage collection");
  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector %ld begin alz=%p-%p *****************\n",
                        melt_nb_garbcoll, melt_startalz, melt_endalz);
  gcc_assert ((char *) melt_startalz < (char *) melt_endalz);
  gcc_assert ((char *) melt_curalz >= (char *) melt_startalz
              && (char *) melt_curalz < (char *) melt_storalz);
  gcc_assert ((char *) melt_storalz < (char *) melt_endalz);
  gcc_assert (melt_scangcvect == NULL);
  {
    unsigned long scanvecsiz = ((1024 + 32 * melt_minorsizekilow) | 0xff) - 2;
    melt_scangcvect = ggc_alloc_cleared_melt_valuevector_st (sizeof(struct melt_valuevector_st)
                      +scanvecsiz * sizeof(melt_ptr_t));
    melt_scangcvect->vv_size = scanvecsiz;
    melt_scangcvect->vv_ulen = 0;
  }

  wanted += wanted / 4 + melt_minorsizekilow * 1000;
  wanted |= 0x3fff;
  wanted++;
  if (wanted < melt_minorsizekilow * sizeof (void *) * 1024)
    wanted = melt_minorsizekilow * sizeof (void *) * 1024;

  melt_is_forwarding = TRUE;
  melt_forward_counter = 0;
  /* Forward the global predefined. We only forward recently touched entry chunks. */
  for (ix = 0; ix < MELT_NB_GLOBAL_CHUNKS; ix++)
    {
      melt_ptr_t* chp = NULL;
      int j = 0;
      if (!melt_touchedglobalchunk[ix])
        continue;
      chp = melt_globalptrs + ix * MELT_GLOBAL_ENTRY_CHUNK;
      for (j=0; j<MELT_GLOBAL_ENTRY_CHUNK; j++)
        MELT_FORWARDED(chp[j]);
      melt_touchedglobalchunk[ix] = false;
    };

  /* Call the forwarding of every plain and extension modules */
  int nbmod = Melt_Module::nb_modules();
  for (ix = 1; ix <= nbmod; ix++)
    {
      Melt_Module* cmod = Melt_Module::unsafe_nth_module (ix);
      gcc_assert (cmod != NULL && cmod->valid_magic());
      cmod->run_forwarding();
    };

  /* Forward the MELT frames */
  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector all classy frames top @%p",
                        (void*) melt_top_call_frame);
  for (Melt_CallProtoFrame *cfr = melt_top_call_frame;
       cfr != NULL;
       cfr = cfr->_meltcf_prev)
    {
      melt_debuggc_eprintf ("melt_minor_copying_garbage_collector forwardingclassyframe %p", (void*)cfr);
      cfr->melt_forward_values ();
      melt_debuggc_eprintf ("melt_minor_copying_garbage_collector forwardedclassyframe %p", (void*)cfr);
    };

  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector %ld done forwarding",
                        melt_nb_garbcoll);
  melt_is_forwarding = FALSE;

  /* Scan the store list.  */
  for (storp = (melt_ptr_t *) melt_storalz;
       (char *) storp < (char *) melt_endalz; storp++)
    {
      if (*storp)
        melt_scanning (*storp);
    }
  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector %ld scanned store list",
                        melt_nb_garbcoll);

  memset (melt_touched_cache, 0, sizeof (melt_touched_cache));

  /* Sort of Cheney loop; http://en.wikipedia.org/wiki/Cheney%27s_algorithm */
  gcc_assert (melt_scangcvect != NULL);
  while (melt_scangcvect->vv_ulen > 0)
    {
      unsigned long vlen = melt_scangcvect->vv_ulen;
      melt_ptr_t p = melt_scangcvect->vv_tab[vlen-1];
      melt_scangcvect->vv_tab[vlen] = NULL;
      melt_scangcvect->vv_ulen = vlen-1;
      if (!p)
        continue;
      melt_scanning (p);
    }
  memset (melt_scangcvect, 0,
          sizeof (struct melt_valuevector_st)
          +melt_scangcvect->vv_size * sizeof(melt_ptr_t));
  ggc_free (melt_scangcvect), melt_scangcvect = NULL;

  melt_delete_unmarked_new_specialdata ();

  /* Free the previous young zone and allocate a new one.  */
  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector %ld freeing alz=%p-%p",
                        melt_nb_garbcoll, melt_startalz, melt_endalz);
  melt_free_young_gc_zone ();
  melt_kilowords_sincefull += wanted / (1024 * sizeof (void *));
  melt_allocate_young_gc_zone (wanted);
  melt_debuggc_eprintf ("melt_minor_copying_garbage_collector ending %ld allocated alz=%p-%p",
                        melt_nb_garbcoll, melt_startalz, melt_endalz);
}


/* Plugin callback started at beginning of GGC, to run a minor copying
   MELT GC.  */
static void
melt_ggcstart_callback (void *gcc_data ATTRIBUTE_UNUSED,
                        void* user_data ATTRIBUTE_UNUSED)
{
  /* Run the minor GC if the birth region has been used, or if its
     store part is non empty (this covers the rare case when no MELT
     values have been allocated, but some have been written into).  */
  if (melt_startalz != NULL && melt_curalz != NULL
      && melt_storalz != NULL && melt_initialstoralz != NULL
      && ((char *) melt_curalz > (char *) melt_startalz
          || melt_storalz < melt_initialstoralz))
    {
      if (melt_prohibit_garbcoll)
        melt_fatal_error ("MELT minor garbage collection prohibited from GGC start callback (with %ld young Kilobytes)",
                          (((char *) melt_curalz - (char *) melt_startalz))>>10);
      melt_debuggc_eprintf
      ("melt_ggcstart_callback need a minor copying GC with %ld young Kilobytes\n",
       (((char *) melt_curalz - (char *) melt_startalz))>>10);
      melt_minor_copying_garbage_collector (0);
    }
}



static long
melt_clear_old_specialdata (void)
{
  long nboldspecdata = 0;
  struct meltspecialdata_st *specda = NULL;
  struct meltspecialdata_st *nextspecda = NULL;
  /* clear our mark fields on old special list before running Ggc. */
  for (specda = melt_oldspecdatalist; specda != NULL; specda = nextspecda)
    {
      specda->meltspec_mark = 0;
      nextspecda = specda->meltspec_next;
      nboldspecdata++;

#if MELT_HAVE_RUNTIME_DEBUG > 0
      if (melt_alptr_1 && (void*)melt_alptr_1 == (void*)specda)
        {
          unsigned mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_clear_old_specialdata oldmark special alptr_1 %p mag %d\n",  melt_alptr_1, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_clear_old_specialdata #%ld clear oldmark special alptr_1 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_1, mag);
          melt_break_alptr_1 ("melt_clear_old_specialdata oldmark special alptr_1");
        }
      if (melt_alptr_2 && (void*)melt_alptr_2 == (void*)specda)
        {
          unsigned mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_clear_old_specialdata oldmark special alptr_2 %p mag %d\n",  melt_alptr_2, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_clear_old_specialdata #%ld clear oldmark  special alptr_2 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_2, mag);
          melt_break_alptr_2 ("melt_clear_old_specialdata oldmark special alptr_2");
        }
#endif /* MELT_HAVE_DEBUG */
    };
  return nboldspecdata;
}


static void
melt_delete_unmarked_old_specialdata (void)
{
  struct meltspecialdata_st *specda = NULL;
  struct meltspecialdata_st *nextspecda = NULL;
  struct meltspecialdata_st **prevspecdaptr = NULL;
  /* Delete the unmarked specials.  */
  prevspecdaptr = &melt_oldspecdatalist;
  for (specda = melt_oldspecdatalist; specda != NULL; specda = nextspecda)
    {
      nextspecda = specda->meltspec_next;

#if MELT_HAVE_RUNTIME_DEBUG > 0
      if (melt_alptr_1 && (void*)melt_alptr_1 == (void*)specda)
        {
          int mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_delete_unmarked_old_specialdata alptr_1 %p mag %d\n",  melt_alptr_1, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_delete_unmarked_old_specialdata #%ld old special alptr_1 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_1, mag);
          melt_break_alptr_1 ("melt_delete_unmarked_old_specialdata alptr_1");
        }
      if (melt_alptr_2 && (void*)melt_alptr_2 == (void*)specda)
        {
          int mag = specda->discr->meltobj_magic;
          fprintf (stderr, "melt_delete_unmarked_old_specialdata alptr_2 %p mag %d\n",  melt_alptr_2, mag);
          fflush (stderr);
          melt_debuggc_eprintf("melt_delete_unmarked_old_specialdata #%ld old special alptr_2 %p mag %d",
                               melt_nb_garbcoll, melt_alptr_2, mag);
          melt_break_alptr_2 ("melt_delete_unmarked_old_specialdata alptr_2");
        }
#endif /*MELT_HAVE_DEBUG*/

      melt_debuggc_eprintf ("melt_delete_unmarked_old_specialdata deletespecloop old specp %p mark %d",
                            (void*)specda, specda->meltspec_mark);
      /* We test both the mark field, if mark_hook is really working in
         gengtype, and the result of ggc_marked_p, for GCC versions
         where it is not working. mark_hook don't always work in GCC 4.7
         and probably not even in 4.6.  See
         http://gcc.gnu.org/ml/gcc-patches/2012-10/msg00164.html for a
         corrective patch to gengtype.  */
      if (specda->meltspec_mark || ggc_marked_p(specda))
        {
          prevspecdaptr = &specda->meltspec_next;
          continue;
        }
      melt_debuggc_eprintf ("melt_delete_unmarked_old_specialdata deletespecloop deleting old specp %p",
                            (void*)specda);
      melt_delete_specialdata (specda);
      memset (specda, 0, sizeof (*specda));
      ggc_free (specda);
      *prevspecdaptr = nextspecda;
    };
}




/***
 * Our copying garbage collector, based upon GGC which does the full collection.
 ***/
void
melt_garbcoll (size_t wanted, enum melt_gckind_en gckd)
{
  const char* needfullreason = NULL;
  if (melt_prohibit_garbcoll)
    melt_fatal_error ("MELT garbage collection prohibited (wanted %ld)",
                      (long)wanted);
  gcc_assert (melt_scangcvect == NULL);
  melt_nb_garbcoll++;
  if (gckd == MELT_NEED_FULL)
    {
      melt_debuggc_eprintf ("melt_garbcoll explicitly needs full gckd#%d",
                            (int) gckd);
      needfullreason = "explicit";
      melt_nb_fullgc_because_asked ++;
    }

  /* set some parameters if they are cleared.  Should happen once.
     The default values (used in particular in plugin mode) are not
     the minimal ones.  */
  if (melt_minorsizekilow == 0)
    {
      const char* minzstr = melt_argument ("minor-zone");
      melt_minorsizekilow = minzstr? (atol (minzstr))
                            : MELT_DEFAULT_MINORSIZE_KW;
      if (melt_minorsizekilow < MELT_MIN_MINORSIZE_KW)
        melt_minorsizekilow = MELT_MIN_MINORSIZE_KW;
      else if (melt_minorsizekilow > MELT_MAX_MINORSIZE_KW)
        melt_minorsizekilow = MELT_MAX_MINORSIZE_KW;
    }
  if (melt_fullthresholdkilow == 0)
    {
      const char* fullthstr = melt_argument ("full-threshold");
      melt_fullthresholdkilow = fullthstr ? (atol (fullthstr))
                                : MELT_DEFAULT_FULLTHRESHOLD_KW;
      if (melt_fullthresholdkilow < MELT_MIN_FULLTHRESHOLD_KW)
        melt_fullthresholdkilow = MELT_MIN_FULLTHRESHOLD_KW;
      if (melt_fullthresholdkilow < 32*melt_minorsizekilow)
        melt_fullthresholdkilow =  32*melt_minorsizekilow;
      if (melt_fullthresholdkilow > MELT_MAX_FULLTHRESHOLD_KW )
        melt_fullthresholdkilow  = MELT_MAX_FULLTHRESHOLD_KW;
    }
  if (melt_fullperiod == 0)
    {
      const char* fullperstr = melt_argument ("full-period");
      melt_fullperiod = fullperstr ? (atoi (fullperstr))
                        : MELT_DEFAULT_PERIODFULL;
      if (melt_fullperiod < MELT_MIN_PERIODFULL)
        melt_fullperiod = MELT_MIN_PERIODFULL;
      else if (melt_fullperiod > MELT_MAX_PERIODFULL)
        melt_fullperiod = MELT_MAX_PERIODFULL;
    }

  if (!needfullreason &&  gckd > MELT_ONLY_MINOR
      && melt_nb_garbcoll % melt_fullperiod == 0)
    {
      melt_debuggc_eprintf ("melt_garbcoll peridically need full nbgarbcoll %ld fullperiod %d",
                            melt_nb_garbcoll, melt_fullperiod);
      melt_nb_fullgc_because_periodic++;
      needfullreason = "periodic";
    }

  if (!needfullreason && gckd > MELT_ONLY_MINOR
      && melt_kilowords_sincefull > (unsigned long) melt_fullthresholdkilow)
    {
      melt_debuggc_eprintf ("melt_garbcoll need full threshold melt_kilowords_sincefull %ld melt_fullthresholdkilow %ld",
                            melt_kilowords_sincefull, melt_fullthresholdkilow);
      melt_nb_fullgc_because_threshold++;
      needfullreason = "threshold";
    }

  melt_minor_copying_garbage_collector (wanted);

  melt_debuggc_eprintf ("melt_garbcoll melt_forwarded_copy_byte_count=%ld",
                        melt_forwarded_copy_byte_count);
  if (!needfullreason && gckd > MELT_ONLY_MINOR
      && (long) melt_forwarded_copy_byte_count
      > (long) (5*melt_minorsizekilow*(1024*sizeof(void*))))
    {
      melt_kilowords_forwarded
      += melt_forwarded_copy_byte_count/(1024*sizeof(void*));
      melt_debuggc_eprintf ("melt_kilowords_forwarded %ld",
                            melt_kilowords_forwarded);
      melt_forwarded_copy_byte_count = 0;
      melt_nb_fullgc_because_copied++;
      needfullreason = "copied";
    }

  if (needfullreason)
    {
      long nboldspec = 0;
      melt_nb_full_garbcoll++;
      melt_debugeprintf ("melt_garbcoll #%ld fullgarbcoll #%ld",
                         melt_nb_garbcoll, melt_nb_full_garbcoll);
      melt_clear_old_specialdata ();
      melt_debugeprintf ("melt_garbcoll calling gcc_collect #%ld after clearing %ld oldspecial marks", melt_nb_full_garbcoll, nboldspec);
      /* There is no need to force a GGC collection, just to run it, and
         Ggc may decide to skip it.  */
      ggc_collect ();
      melt_debugeprintf ("melt_garbcoll after fullgarbcoll #%ld", melt_nb_full_garbcoll);
      melt_delete_unmarked_old_specialdata ();
      if (melt_verbose_full_gc)
        {
          /* when not asked, the GGC collector displays data, so we can
             add a message and end the line! "*/
          fprintf (stderr, " MELT full gc#%ld/%ld [%s, %ld Kw young, %ld Kw forwarded]\n",
                   melt_nb_full_garbcoll, melt_nb_garbcoll,
                   needfullreason,
                   melt_kilowords_sincefull, melt_kilowords_forwarded);
          fflush (stderr);
        }
      melt_kilowords_sincefull = 0;
      melt_kilowords_forwarded = 0;
      /* end of MELT full garbage collection */
    }
  melt_check_call_frames (MELT_NOYOUNG, "after garbage collection");
  gcc_assert (melt_scangcvect == NULL);
}

static void meltpayload_file_destroy (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);
static char* meltpayload_file_sprint (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);


static struct melt_payload_descriptor_st meltpydescr_file =
{
  /* .meltpyd_magic = */ MELT_PAYLOAD_DESCRIPTOR_MAGIC,
  /* .meltpyd_rank = */ meltpydkind_file,
  /* .meltpyd_name = */ "file",
  /* .meltpyd_data = */ NULL,
  /* .meltpyd_destroy_rout = */ meltpayload_file_destroy,
  /* .meltpyd_sprint_rout = */ meltpayload_file_sprint,
  /* .meltpyd_spare1 =*/ 0,
  /* .meltpyd_spare2 =*/ 0,
  /* .meltpyd_spare3 =*/ 0
};


static void meltpayload_rawfile_destroy (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);
static char* meltpayload_rawfile_sprint (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);
static struct melt_payload_descriptor_st meltpydescr_rawfile =
{
  /* .meltpyd_magic = */ MELT_PAYLOAD_DESCRIPTOR_MAGIC,
  /* .meltpyd_rank = */ meltpydkind_rawfile,
  /* .meltpyd_name = */ "rawfile",
  /* .meltpyd_data = */ NULL,
  /* .meltpyd_destroy_rout = */ meltpayload_rawfile_destroy,
  /* .meltpyd_sprint_rout = */ meltpayload_rawfile_sprint,
  /* .meltpyd_spare1 = */ 0,
  /* .meltpyd_spare2 =*/ 0,
  /* .meltpyd_spare3 =*/ 0
};

static void melt_payload_initialize_static_descriptors (void)
{
  meltpyd_array[meltpydkind_file] = &meltpydescr_file;
  meltpyd_array[meltpydkind_rawfile] = &meltpydescr_rawfile;
}

int melt_payload_register_descriptor (struct melt_payload_descriptor_st*mpd)
{
  unsigned mrk = 0;
  if (!mpd) return 0;
  if (mpd->meltpyd_magic != MELT_PAYLOAD_DESCRIPTOR_MAGIC || !mpd->meltpyd_name)
    melt_fatal_error("MELT cannot register corrupted payload descriptor @%p",
                     (void*) mpd);
  if (mpd->meltpyd_rank > 0 && mpd->meltpyd_rank < MELTPYD_MAX_RANK
      && meltpyd_array[mpd->meltpyd_rank] == mpd)
    return mpd->meltpyd_rank;
  if (mpd->meltpyd_rank != 0)
    melt_fatal_error("MELT cannot register payload descriptor @%p with bad rank %d",
                     (void*) mpd, mpd->meltpyd_rank);
  {
    unsigned r = 0;
    for (r = meltpydkind__last; r < MELTPYD_MAX_RANK && !mrk; r++)
      if (!meltpyd_array[r])
        mrk = r;
  }
  if (!mrk)
    melt_fatal_error("MELT cannot register payload descriptor @%p, table of %d is full",
                     (void*)mpd, MELTPYD_MAX_RANK);
  mpd->meltpyd_rank = mrk;
  meltpyd_array[mrk] = mpd;
  return mrk;
}


static void meltpayload_file_destroy (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);
static char* meltpayload_file_sprint (struct meltspecialdata_st*, const struct melt_payload_descriptor_st*);





static void
meltpayload_rawfile_destroy (struct meltspecialdata_st*sd, const struct melt_payload_descriptor_st*mpd ATTRIBUTE_UNUSED)
{
  if (sd->meltspec_payload.meltpayload_file1)
    fflush (sd->meltspec_payload.meltpayload_file1);
  sd->meltspec_payload.meltpayload_file1 = NULL;
}

static char*
meltpayload_rawfile_sprint (struct meltspecialdata_st*sd, const struct melt_payload_descriptor_st*mpd ATTRIBUTE_UNUSED)
{
  char buf[64];
  if (sd->meltspec_payload.meltpayload_file1)
    snprintf (buf, sizeof(buf), "raw:FILE@%p#%d",
              (void*)(sd->meltspec_payload.meltpayload_file1), fileno (sd->meltspec_payload.meltpayload_file1));
  else
    strcpy (buf, "raw-NULL-FILE");
  return xstrdup (buf);
}



static void
meltpayload_file_destroy (struct meltspecialdata_st*sd, const struct melt_payload_descriptor_st*mpd ATTRIBUTE_UNUSED)
{
  if (sd->meltspec_payload.meltpayload_file1)
    fclose (sd->meltspec_payload.meltpayload_file1);
  sd->meltspec_payload.meltpayload_file1 = NULL;
}

static char*
meltpayload_file_sprint (struct meltspecialdata_st*sd, const struct melt_payload_descriptor_st*mpd ATTRIBUTE_UNUSED)
{
  char buf[64];
  if (sd->meltspec_payload.meltpayload_file1)
    snprintf (buf, sizeof(buf), "FILE@%p#%d",
              (void*)(sd->meltspec_payload.meltpayload_file1), fileno (sd->meltspec_payload.meltpayload_file1));
  else
    strcpy (buf, "NULL-FILE");
  return xstrdup (buf);
}




/* The inline function melt_allocatereserved is the only one
   calling this melt_reserved_allocation_failure function, which
   should never be called. If it is indeed called, you've been bitten
   by a severe bug. In principle melt_allocatereserved should have
   been called with a suitable previous call to meltgc_reserve such
   that all the reserved allocations fits into the reserved size */
void
melt_reserved_allocation_failure (long siz)
{
  /* this function should never really be called */
  melt_fatal_error ("memory corruption in MELT reserved allocation: "
                    "requiring %ld bytes but only %ld available in young zone",
                    siz,
                    (long) ((char *) melt_storalz - (char *) melt_curalz));
}





/** array of about 190 primes gotten by shell command
    /usr/games/primes 3 2000000000 | awk '($1>p+p/8){print $1, ","; p=$1}'  **/
const long melt_primtab[256] =
{
  0,        /* the first entry indexed #0 is 0 to never be used */
  3, 5, 7, 11, 13, 17, 23, 29, 37, 43, 53, 61, 71, 83, 97, 113,
  131, 149, 173, 197, 223, 251, 283, 331, 373, 421, 479, 541,
  613, 691, 787, 887, 1009, 1151, 1297, 1471, 1657, 1867, 2111,
  2377, 2677, 3019, 3407, 3833, 4327, 4871, 5483, 6173, 6947,
  7817, 8803, 9907, 11149, 12547, 14143, 15913, 17903, 20143,
  22669, 25523, 28723, 32321, 36373, 40927, 46049, 51817,
  58309, 65599, 73819, 83047, 93463, 105167, 118343, 133153,
  149803, 168533, 189613, 213319, 239999, 270001, 303767,
  341743, 384469, 432539, 486617, 547453, 615887, 692893,
  779507, 876947, 986567, 1109891, 1248631, 1404721, 1580339,
  1777891, 2000143, 2250163, 2531443, 2847893, 3203909,
  3604417, 4054987, 4561877, 5132117, 5773679, 6495389,
  7307323, 8220743, 9248339, 10404403, 11704963, 13168091,
  14814103, 16665881, 18749123, 21092779, 23729411, 26695609,
  30032573, 33786659, 38010019, 42761287, 48106453, 54119761,
  60884741, 68495347, 77057297, 86689469, 97525661, 109716379,
  123430961, 138859837, 156217333, 175744531, 197712607,
  222426683, 250230023, 281508827, 316697431, 356284619,
  400820209, 450922753, 507288107, 570699121, 642036517,
  722291083, 812577517, 914149741,
#if HOST_BITS_PER_LONG >= 64
  1028418463, 1156970821, 1301592203,
  1464291239, 1647327679, 1853243677, 2084899139, 2345511541,
  2638700497, 2968538081, 3339605383, 3757056091, 4226688133,
  4755024167, 5349402193, 6018077509, 6770337239, 7616629399,
  8568708139, 9639796667, 10844771263, 12200367671,
  13725413633, 15441090347, 17371226651, 19542629983,
  21985458749, 24733641113, 27825346259, 31303514549,
  35216453869, 39618510629, 44570824481, 50142177559,
#endif
  0, 0
};

/* index of entry to get or add an attribute in an mapobject (or -1 on error) */
static inline int
unsafe_index_mapobject (struct entryobjectsmelt_st *tab,
                        meltobject_ptr_t attr, int siz)
{
  int da = 0, ix = 0, frix = -1;
  unsigned h = 0;
  if (!tab)
    return -1;
  da = attr->meltobj_class->meltobj_magic;
  if (da == MELTOBMAG_OBJECT)
    h = ((struct meltobject_st *) attr)->obj_hash;
  else
    return -1;
  h = h % siz;
  for (ix = h; ix < siz; ix++)
    {
      meltobject_ptr_t curat = tab[ix].e_at;
      if (curat == attr)
        return ix;
      else if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
    }
  for (ix = 0; ix < (int) h; ix++)
    {
      meltobject_ptr_t curat = tab[ix].e_at;
      if (curat == attr)
        return ix;
      else if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
    }
  if (frix >= 0)
    return frix;    /* found some place in a table with
           deleted entries but no empty
           entries */
  return -1;      /* entirely full, should not happen */
}


melt_ptr_t
meltgc_new_int (meltobject_ptr_t discr_p, long num)
{
  MELT_ENTERFRAME (2, NULL);
#define newintv meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define int_newintv ((struct meltint_st*)(newintv))
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_CONSTANT_INTEGER);
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_INT)
    goto end;
  newintv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltint_st), 0);
  int_newintv->discr = object_discrv;
  int_newintv->val = num;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newintv;
#undef newintv
#undef discrv
#undef int_newintv
#undef object_discrv
}



melt_ptr_t
meltgc_new_double (meltobject_ptr_t discr_p, double num)
{
  MELT_ENTERFRAME (2, NULL);
#define newdblv meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define dbl_newdblv ((struct meltdouble_st*)(newdblv))
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_CONSTANT_DOUBLE);
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_DOUBLE)
    goto end;
  newdblv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltdouble_st), 0);
  dbl_newdblv->discr = object_discrv;
  dbl_newdblv->val = num;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newdblv;
#undef newintv
#undef discrv
#undef int_newintv
#undef object_discrv
}


melt_ptr_t
meltgc_new_mixint (meltobject_ptr_t discr_p,
                   melt_ptr_t val_p, long num)
{
  MELT_ENTERFRAME (3, NULL);
#define newmix  meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define valv    meltfram__.mcfr_varptr[2]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mix_newmix ((struct meltmixint_st*)(newmix))
  newmix = NULL;
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_MIXED_INTEGER);
  valv = val_p;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MIXINT)
    goto end;
  newmix = (melt_ptr_t) meltgc_allocate (sizeof (struct meltmixint_st), 0);
  mix_newmix->discr = object_discrv;
  mix_newmix->intval = num;
  mix_newmix->ptrval = (melt_ptr_t) valv;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmix;
#undef newmix
#undef valv
#undef discrv
#undef mix_newmix
#undef object_discrv
}



melt_ptr_t
meltgc_new_mixloc (meltobject_ptr_t discr_p,
                   melt_ptr_t val_p, long num, location_t loc)
{
  MELT_ENTERFRAME (3, NULL);
#define newmix  meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define valv    meltfram__.mcfr_varptr[2]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mix_newmix ((struct meltmixloc_st*)(newmix))
  newmix = NULL;
  discrv = (melt_ptr_t) discr_p;
  valv = val_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_MIXED_LOCATION);
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MIXLOC)
    goto end;
  newmix = (melt_ptr_t) meltgc_allocate (sizeof (struct meltmixloc_st), 0);
  mix_newmix->discr = object_discrv;
  mix_newmix->intval = num;
  mix_newmix->ptrval = (melt_ptr_t) valv;
  mix_newmix->locval = loc;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmix;
#undef newmix
#undef valv
#undef discrv
#undef mix_newmix
#undef object_discrv
}


melt_ptr_t
meltgc_new_mixbigint_mpz (meltobject_ptr_t discr_p,
                          melt_ptr_t val_p, mpz_t mp)
{
  unsigned numb = 0, blen = 0;
  size_t wcnt = 0;
  MELT_ENTERFRAME (3, NULL);
#define newbig  meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define valv    meltfram__.mcfr_varptr[2]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mix_newbig ((struct meltmixbigint_st*)(newbig))
  newbig = NULL;
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_MIXED_BIGINT);
  valv = val_p;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MIXBIGINT)
    goto end;
  if (!mp)
    goto end;
  numb = 8*sizeof(mix_newbig->tabig[0]);
  blen = (mpz_sizeinbase (mp, 2) + numb) / numb + 1;
  newbig = (melt_ptr_t) meltgc_allocate (sizeof (struct meltmixbigint_st),
                                         blen*sizeof(mix_newbig->tabig[0]));
  mix_newbig->discr = object_discrv;
  mix_newbig->ptrval = (melt_ptr_t) valv;
  mix_newbig->negative = (mpz_sgn (mp)<0);
  mix_newbig->biglen = blen;
  mpz_export (mix_newbig->tabig, &wcnt,
              /*most significant word first */ 1,
              sizeof(mix_newbig->tabig[0]),
              /*native endian*/ 0,
              /* no nails bits */ 0,
              mp);
  gcc_assert(wcnt <= blen);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newbig;
#undef newbig
#undef valv
#undef discrv
#undef mix_newbig
#undef object_discrv
}


melt_ptr_t
meltgc_new_real (meltobject_ptr_t discr_p, REAL_VALUE_TYPE r)
{
  MELT_ENTERFRAME (2, NULL);
#define resv   meltfram__.mcfr_varptr[0]
#define discrv meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define real_resv ((struct meltreal_st*) resv)
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = (melt_ptr_t) MELT_PREDEF (DISCR_REAL);
  if (object_discrv->meltobj_magic != MELTOBMAG_REAL)
    goto end;
  resv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltreal_st), 0);
  real_resv->discr = object_discrv;
  real_resv->val = r;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef resv
#undef discrv
#undef object_discrv
#undef real_resv
}

/* allocate a new routine object of given DISCR and of length LEN,
   with a DESCR-iptive string a a PROC-edure */
meltroutine_ptr_t
meltgc_new_routine (meltobject_ptr_t discr_p,
                    unsigned len, const char *descr,
                    meltroutfun_t * proc)
{
  MELT_ENTERFRAME (2, NULL);
#define newroutv meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define obj_discrv ((meltobject_ptr_t)(discrv))
#define rou_newroutv ((meltroutine_ptr_t)(newroutv))
  newroutv = NULL;
  discrv = (melt_ptr_t) discr_p;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT
      || obj_discrv->meltobj_magic != MELTOBMAG_ROUTINE || !descr || !descr[0]
      || !proc || len > MELT_MAXLEN)
    goto end;
  newroutv =
    (melt_ptr_t) meltgc_allocate (sizeof (struct meltroutine_st),
                                  len * sizeof (void *));
  rou_newroutv->discr = (meltobject_ptr_t) discrv;
  rou_newroutv->nbval = len;
  rou_newroutv->routfunad = proc;
  strncpy (rou_newroutv->routdescr, descr, MELT_ROUTDESCR_LEN - 1);
  rou_newroutv->routdescr[MELT_ROUTDESCR_LEN - 1] = (char) 0;
end:
  MELT_EXITFRAME ();
  return (meltroutine_ptr_t) newroutv;
#undef newroutv
#undef discrv
#undef obj_discrv
#undef rou_newroutv
}

void
meltgc_set_routine_data (melt_ptr_t rout_p, melt_ptr_t data_p)
{
  MELT_ENTERFRAME (2, NULL);
#define routv meltfram__.mcfr_varptr[0]
#define datav  meltfram__.mcfr_varptr[1]
  routv = rout_p;
  datav = data_p;
  if (melt_magic_discr ((melt_ptr_t) routv) == MELTOBMAG_ROUTINE)
    {
      ((meltroutine_ptr_t) routv)->routdata = (melt_ptr_t) datav;
      meltgc_touch_dest (routv, datav);
    }
  MELT_EXITFRAME ();
#undef routv
#undef datav
}


void
meltgc_set_hook_data (melt_ptr_t hook_p, melt_ptr_t data_p)
{
  MELT_ENTERFRAME(2, NULL);
#define hookv meltfram__.mcfr_varptr[0]
#define datav meltfram__.mcfr_varptr[1]
  hookv = hook_p;
  datav = data_p;
  if (melt_magic_discr((melt_ptr_t) hookv) == MELTOBMAG_HOOK)
    {
      ((melthook_ptr_t) hookv)->hookdata = (melt_ptr_t) datav;
      meltgc_touch_dest (hookv, datav);
    }
  MELT_EXITFRAME();
#undef hookv
#undef datav
}

const char*
melt_hook_interned_name (melt_ptr_t hk)
{
  if (melt_magic_discr((melt_ptr_t) hk) != MELTOBMAG_HOOK)
    return NULL;
  return melt_intern_cstring (((melthook_ptr_t)hk)->hookname);
}

melt_ptr_t
meltgc_hook_name_string (melt_ptr_t hook_p)
{
  MELT_ENTERFRAME(2, NULL);
#define hookv meltfram__.mcfr_varptr[0]
#define strv  meltfram__.mcfr_varptr[1]
  hookv = hook_p;
  if (melt_magic_discr((melt_ptr_t) hookv) == MELTOBMAG_HOOK)
    {
      strv = meltgc_new_stringdup ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),
                                   ((melthook_ptr_t)hookv)->hookname);
    }
  MELT_EXITFRAME();
  return (melt_ptr_t)strv;
#undef hookv
#undef strv
}

meltclosure_ptr_t
meltgc_new_closure (meltobject_ptr_t discr_p,
                    meltroutine_ptr_t rout_p, unsigned len)
{
  MELT_ENTERFRAME (3, NULL);
#define newclosv  meltfram__.mcfr_varptr[0]
#define discrv    meltfram__.mcfr_varptr[1]
#define routv     meltfram__.mcfr_varptr[2]
#define clo_newclosv ((meltclosure_ptr_t)(newclosv))
#define obj_discrv   ((meltobject_ptr_t)(discrv))
#define rou_routv    ((meltroutine_ptr_t)(routv))
  discrv = (melt_ptr_t) discr_p;
  routv = (melt_ptr_t) rout_p;
  newclosv = NULL;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT
      || obj_discrv->meltobj_magic != MELTOBMAG_CLOSURE
      || melt_magic_discr ((melt_ptr_t) (routv)) != MELTOBMAG_ROUTINE
      || len > MELT_MAXLEN)
    goto end;
  newclosv =
    (melt_ptr_t) meltgc_allocate (sizeof (struct meltclosure_st),
                                  sizeof (void *) * len);
  clo_newclosv->discr = (meltobject_ptr_t) discrv;
  clo_newclosv->rout = (meltroutine_ptr_t) routv;
  clo_newclosv->nbval = len;
end:
  MELT_EXITFRAME ();
  return (meltclosure_ptr_t) newclosv;
#undef newclosv
#undef discrv
#undef routv
#undef clo_newclosv
#undef obj_discrv
#undef rou_routv
}



struct meltstrbuf_st *
meltgc_new_strbuf (meltobject_ptr_t discr_p, const char *str)
{
  int slen = 0, blen = 0, ix = 0;
  MELT_ENTERFRAME (2, NULL);
#define newbufv  meltfram__.mcfr_varptr[0]
#define discrv   meltfram__.mcfr_varptr[1]
#define buf_newbufv ((struct meltstrbuf_st*)(newbufv))
  discrv = (melt_ptr_t) discr_p;
  newbufv = NULL;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (((meltobject_ptr_t) (discrv))->meltobj_magic != MELTOBMAG_STRBUF)
    goto end;
  if (str)
    slen = strlen (str);
  gcc_assert (slen < MELT_MAXLEN);
  slen += slen / 5 + 40;
  for (ix = 2; (blen = melt_primtab[ix]) != 0 && blen < slen; ix++);
  gcc_assert (blen != 0);
  newbufv =
    (melt_ptr_t) meltgc_allocate (offsetof
                                  (struct meltstrbuf_st, buf_space), blen + 1);
  buf_newbufv->discr = (meltobject_ptr_t) discrv;
  buf_newbufv->bufzn = buf_newbufv->buf_space;
  buf_newbufv->buflenix = ix;
  buf_newbufv->bufstart = 0;
  if (str)
    {
      strcpy (buf_newbufv->bufzn, str);
      buf_newbufv->bufend = strlen (str);
    }
  else
    buf_newbufv->bufend = 0;
end:
  MELT_EXITFRAME ();
  return (struct meltstrbuf_st *) newbufv;
#undef newbufv
#undef discrv
#undef buf_newbufv
}

/* we need to be able to compute the length of the last line of a
   FILE* filled by MELT output primitives; very often this FILE* will
   be stdout or stderr; and we don't care that much if the computed
   length of the last [i.e. current] line is wrong. So we keep an
   array of positions in FILE*, indexed by their fileno, which we
   suppose is small */
#define MELTMAXFILE 512
static long lasteol[MELTMAXFILE];


long
melt_output_length (melt_ptr_t out_p)
{
  if (!out_p)
    return 0;
  switch (melt_magic_discr (out_p))
    {
    case MELTOBMAG_STRBUF:
    {
      struct meltstrbuf_st *sb = (struct meltstrbuf_st *) out_p;
      if (sb->bufend >= sb->bufstart)
        return sb->bufend - sb->bufstart;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      struct meltspecialdata_st* spd = (struct meltspecialdata_st*) out_p;
      if (spd->meltspec_kind == meltpydkind_file ||
          spd->meltspec_kind == meltpydkind_rawfile)
        {
          FILE *fil = spd->meltspec_payload.meltpayload_file1;
          if (fil)
            {
              long off = ftell (fil);
              return off;
            }
        }
    }
    break;
    default:
      break;
    }
  return 0;
}



void
meltgc_strbuf_reserve (melt_ptr_t outbuf_p, unsigned reslen)
{
  unsigned blen = 0;
  unsigned slen = 0;
  MELT_ENTERFRAME (1, NULL);
#define outbufv  meltfram__.mcfr_varptr[0]
#define buf_outbufv  ((struct meltstrbuf_st*)(outbufv))
  outbufv = outbuf_p;
  if (!outbufv || melt_magic_discr ((melt_ptr_t) (outbufv)) != MELTOBMAG_STRBUF)
    goto end;
  blen = melt_primtab[buf_outbufv->buflenix];
  gcc_assert (blen > 0);
  gcc_assert (buf_outbufv->bufstart <= buf_outbufv->bufend
              && buf_outbufv->bufend < (unsigned) blen);
  if (buf_outbufv->bufend + reslen + 1 < blen)
    /* simplest case, there is enough space without changing the strbuf */
    goto end;
  slen = buf_outbufv->bufend - buf_outbufv->bufstart;
  if (slen + reslen + 2 < blen)
    {
      /* the strbuf has enough space, but it needs to be moved... */
      memmove (buf_outbufv->bufzn,
               buf_outbufv->bufzn + buf_outbufv->bufstart, slen);
      buf_outbufv->bufstart = 0;
      buf_outbufv->bufend = slen;
      memset (buf_outbufv->bufzn + slen, 0, blen-slen-1);
    }
  else
    {
      unsigned long newblen = 0;
      int newix = 0;
      unsigned long newsiz = slen + reslen + 10;
      bool wasyoung = FALSE;
      newsiz += newsiz/8;
#if MELT_HAVE_RUNTIME_DEBUG > 0
      /* to help catching monster buffer overflow */
      if (newsiz > MELT_BIGLEN)
        {
          static unsigned long sbufthreshold;
          if (newsiz > sbufthreshold && melt_flag_debug)
            {
              unsigned int shownsize = 0;
              long rnd = melt_lrand() & 0xffffff;
              sbufthreshold = ((newsiz + (sbufthreshold / 4)) | 0xff) + 1;
              shownsize = (int)(5000 + (sbufthreshold/(MELT_BIGLEN/16)));
              gcc_assert ((shownsize * 3L) < newsiz);
              /* we generate a quasirandom marker to ease searching */
              melt_debugeprintf_raw("\n\n##########%06lx##\n", rnd);
              melt_debugeprintf
              ("MELT string buffer @%p of length %ld growing very big to %ld\n",
               (void*) outbufv,
               (long) (buf_outbufv->bufend -  buf_outbufv->bufstart),
               newsiz);
              melt_debugeprintf("MELT big string buffer starts with %d bytes:\n%.*s\n",
                                shownsize, shownsize,
                                buf_outbufv->bufzn + buf_outbufv->bufstart);
              melt_debugeprintf_raw("##########%06lx##\n", rnd);
              melt_debugeprintf("MELT big string buffer ends with %d bytes:\n%.*s\n",
                                shownsize, shownsize,
                                buf_outbufv->bufzn + buf_outbufv->bufend
                                - shownsize);
              melt_debugeprintf_raw("##########%06lx##\n", rnd);
              melt_dbgshortbacktrace ("MELT big string buffer", 20);
            }
        }
#endif
      if (newsiz > MELT_MAXLEN)
        melt_fatal_error ("MELT string buffer overflow, needed %ld bytes = %ld Megabytes!",
                          (long)newsiz, (long)newsiz>>20);
      for (newix = buf_outbufv->buflenix + 1;
           (newblen = melt_primtab[newix]) != 0
           && newblen < newsiz; newix++) {};
      gcc_assert (newblen != 0) /* Otherwise, the required buffer is too big.  */;
      /* we need to allocate more memory for the buffer... */
      if (melt_is_young (outbufv))
        {
          wasyoung = TRUE;
          meltgc_reserve (8*sizeof(void*) + sizeof(struct meltstrbuf_st) + newblen);
          /* The previous reservation may have triggered a MELT minor
             collection and have copied outbufv out of the young zone,
             so we test again for youngness. */
        }
      if (wasyoung && melt_is_young (outbufv))
        {
          /* If the buffer is still young, we do have enough place in the young birth region. */
          char* newb = NULL;
          gcc_assert (melt_is_young (buf_outbufv->bufzn));
          newb = (char*)  melt_allocatereserved (newblen + 1, 0);
          memcpy (newb, buf_outbufv->bufzn + buf_outbufv->bufstart, slen);
          newb[slen] = 0;
          buf_outbufv->buflenix = newix;
          buf_outbufv->bufzn = newb;
          buf_outbufv->bufstart = 0;
          buf_outbufv->bufend = slen;
        }
      else
        {
          /* The buffer is old, in Ggc heap. */
          char* newzn = NULL;
          char* oldzn = buf_outbufv->bufzn;
          gcc_assert (!melt_is_young (oldzn));

          newzn = (char*) ggc_alloc_atomic (newblen+1);
          memcpy (newzn, oldzn + buf_outbufv->bufstart, slen);
          newzn[slen] = 0;
          memset (oldzn, 0, slen<100?slen/2:50);
          buf_outbufv->buflenix = newix;
          buf_outbufv->bufzn = newzn;
          buf_outbufv->bufstart = 0;
          buf_outbufv->bufend = slen;
          ggc_free (oldzn);
        }
      meltgc_touch ((melt_ptr_t)outbufv);
    }
end:
  MELT_EXITFRAME ();
#undef outbufv
#undef buf_outbufv
}


void
meltgc_add_out_raw_len (melt_ptr_t outbuf_p, const char *str, int slen)
{
  int blen = 0;
  MELT_ENTERFRAME (2, NULL);
#define outbufv  meltfram__.mcfr_varptr[0]
#define buf_outbufv  ((struct meltstrbuf_st*)(outbufv))
#define spec_outbufv  ((struct meltspecial_st*)(outbufv))
#define spda_outbufv  ((struct meltspecialdata_st*)(outbufv))
  outbufv = outbuf_p;
  if (!str)
    goto end;
  if (slen<0)
    slen = strlen (str);
  if (slen<=0)
    goto end;
  switch (melt_magic_discr ((melt_ptr_t) (outbufv)))
    {
    case MELTOBMAG_SPECIAL_DATA:
    {
      if (spda_outbufv->meltspec_kind == meltpydkind_file
          || spda_outbufv->meltspec_kind == meltpydkind_rawfile)
        {
          FILE *f = spda_outbufv->meltspec_payload.meltpayload_file1;
          if (f)
            {
              int fno = fileno (f);
              const char* eol = NULL;
              long fp = ftell (f);
              (void) fwrite(str, (size_t)slen, (size_t)1, f);
              if (fno < MELTMAXFILE && fno >= 0 && (eol = strchr(str, '\n'))
                  && eol-str < slen)
                lasteol[fno] = fp + (eol-str);
            }
        }
    }
    break;
    case MELTOBMAG_STRBUF:
      gcc_assert (!melt_is_young (str));
      blen = melt_primtab[buf_outbufv->buflenix];
      gcc_assert (blen > 0);
      gcc_assert (buf_outbufv->bufstart <= buf_outbufv->bufend
                  && buf_outbufv->bufend < (unsigned) blen);
      if ((int) buf_outbufv->bufend + slen + 2 < blen)
        {
          /* simple case, just copy at end */
          strncpy (buf_outbufv->bufzn + buf_outbufv->bufend, str, slen);
          buf_outbufv->bufend += slen;
          buf_outbufv->bufzn[buf_outbufv->bufend] = 0;
        }
      else if ((int) buf_outbufv->bufstart > (int) 0
               && (int) buf_outbufv->bufend -
               (int) buf_outbufv->bufstart + (int) slen + 2 < (int) blen)
        {
          /* should move the buffer to fit */
          int oldlen = buf_outbufv->bufend - buf_outbufv->bufstart;
          gcc_assert (oldlen >= 0);
          memmove (buf_outbufv->bufzn,
                   buf_outbufv->bufzn + buf_outbufv->bufstart, oldlen);
          buf_outbufv->bufstart = 0;
          strncpy (buf_outbufv->bufzn + oldlen, str, slen);
          buf_outbufv->bufend = oldlen + slen;
          buf_outbufv->bufzn[buf_outbufv->bufend] = 0;
        }
      else
        {
          /* should grow the buffer to fit */
          int oldlen =  buf_outbufv->bufend - buf_outbufv->bufstart;
          gcc_assert (oldlen >= 0);
          meltgc_strbuf_reserve ((melt_ptr_t) outbufv, slen + (slen+oldlen)/8 + 30);
          strncpy (buf_outbufv->bufzn + buf_outbufv->bufend, str, slen);
          buf_outbufv->bufend += slen;
          buf_outbufv->bufzn[buf_outbufv->bufend] = 0;
        }
      break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef outbufv
#undef buf_outbufv
#undef spec_outbufv
#undef spda_outbufv
}

void
meltgc_add_out_raw (melt_ptr_t out_p, const char *str)
{
  meltgc_add_out_raw_len(out_p, str, -1);
}

void
meltgc_add_out (melt_ptr_t out_p, const char *str)
{
  char sbuf[80];
  char *cstr = NULL;
  int slen = 0;
  if (str)
    slen = strlen (str);
  if (slen <= 0)
    return;
  if (slen < (int) sizeof (sbuf) - 1)
    {
      memset (sbuf, 0, sizeof (sbuf));
      strcpy (sbuf, str);
      meltgc_add_out_raw (out_p, sbuf);
    }
  else
    {
      cstr = xstrdup (str);
      meltgc_add_out_raw (out_p, cstr);
      free (cstr);
    }
}



void
meltgc_add_out_cstr_len_mode (melt_ptr_t outbuf_p, const char *str, int slen,
                              enum melt_coutput_mode_en omode)
{
  const char *ps = NULL;
  char *pd = NULL;
  char *lastnl = NULL;
  char *encstr = NULL;
  /* duplicate the given string either on stack in tinybuf or in
     xcalloc-ed buffer */
  char *dupstr = NULL;
  int encsiz = 0;
  char tinybuf[80];
  if (!str)
    return;
  if (slen<0)
    slen = strlen(str);
  if (slen<(int) sizeof(tinybuf)-3)
    {
      memset (tinybuf, 0, sizeof(tinybuf));
      memcpy (tinybuf, str, slen);
      dupstr = tinybuf;
    }
  else
    {
      dupstr = (char*) xcalloc (slen + 2, 1);
      memcpy (dupstr, str, slen);
    }
  /* at most four characters e.g. \xAB per original character, but
     occasionally a backslashed newline for readability */
  encsiz = slen+slen/16+8;
  encstr = (char *) xcalloc (encsiz+4, 1);
  pd = encstr;
  for (ps = dupstr; *ps; ps++)
    {
      int curlinoff = pd - (lastnl?lastnl:encstr);
      if (pd - encstr > encsiz - 8)
        {
          int newsiz = ((5*encsiz/4 + slen/8 + 5)|7);
          char *newstr = (char*)xcalloc (newsiz+1, 1);
          size_t curln = pd - encstr;
          memcpy (newstr, encstr, curln);
          free (encstr), encstr = newstr;
          encsiz = newsiz;
          pd = encstr + curln;
        }
      if (ps[1] && ps[2] && ps[3] && curlinoff > 65 && ps[4])
        {
          if ((!ISALNUM(ps[0]) && ps[0] != '_')
              || ISSPACE(ps[0])
              || curlinoff > 76)
            {
              strcpy (pd, "\\" "\n");
              pd += 2;
              lastnl = pd;
            }
        }
      switch (*ps)
        {
#define ADDS(S) strcpy(pd, S); pd += sizeof(S)-1; break
        case '\n':
          if (ps[1] && ps[2] && curlinoff > 32)
            {
              strcpy (pd, "\\" "\n");
              pd += 2;
              lastnl = pd;
            }
          ADDS ("\\n");
        case '\r':
          ADDS ("\\r");
        case '\t':
          ADDS ("\\t");
        case '\v':
          ADDS ("\\v");
        case '\f':
          ADDS ("\\f");
        case '\'':
          ADDS ("\\\'");
        case '\"':
          ADDS ("\\\"");
        case '\\':
          ADDS ("\\\\");
#undef ADDS
        default:
          if ((unsigned char)(*ps) < (unsigned char)0x7f && ISPRINT (*ps))
            *(pd++) = *ps;
          else
            {
              switch (omode)
                {
                case MELTCOUT_ASCII:
                {
                  sprintf (pd, "\\%03o", (*ps) & 0xff);
                  pd += 4;
                }
                break;
                case MELTCOUT_UTF8JSON:
                  if ((unsigned char)(*ps) < 0xff && ISCNTRL(*ps))
                    {
                      sprintf (pd, "\\u%04x", (*ps) & 0xff);
                      pd += 6;
                    }
                  else
                    {
                      // if the source string is UTF-8, the UTF-8 multibyte characters are kept verbatim!
                      *(pd++) = *ps;
                    }
                  break;
                }
            }
        }
    };
  if (dupstr && dupstr != tinybuf)
    free (dupstr);
  meltgc_add_out_raw (outbuf_p, encstr);
  free (encstr);
}


void
meltgc_add_out_ccomment (melt_ptr_t outbuf_p, const char *str)
{
  int slen = str ? strlen (str) : 0;
  const char *ps = NULL;
  char *pd = NULL;
  char *cstr = NULL;
  if (!str || !str[0])
    return;
  cstr = (char *) xcalloc (slen + 4, 4);
  pd = cstr;
  for (ps = str; *ps; ps++)
    {
      if (ps[0] == '/' && ps[1] == '*')
        {
          pd[0] = '/';
          pd[1] = '+';
          pd += 2;
          ps++;
        }
      else if (ps[0] == '*' && ps[1] == '/')
        {
          pd[0] = '+';
          pd[1] = '/';
          pd += 2;
          ps++;
        }
      else
        *(pd++) = *ps;
    };
  meltgc_add_out_raw (outbuf_p, cstr);
  free (cstr);
}

void
meltgc_add_out_cident (melt_ptr_t outbuf_p, const char *str)
{
  int slen = str ? strlen (str) : 0;
  char *dupstr = 0;
  const char *ps = 0;
  char *pd = 0;
  char tinybuf[80];
  if (!str || !str[0])
    return;
  if (2*slen < (int) sizeof (tinybuf) - 4)
    {
      memset (tinybuf, 0, sizeof (tinybuf));
      dupstr = tinybuf;
    }
  else
    dupstr = (char *) xcalloc (2*slen + 4, 1);
  if (str)
    for (ps = (const char *) str, pd = dupstr; *ps; ps++)
      {
        if (ISALNUM (*ps))
          *(pd++) = TOUPPER(*ps);
        else if(*ps=='_')
          *(pd++) = *ps;
        else switch (*ps)
            {
            case '-':
              pd[0]='m';
              pd[1]='i';
              pd+=2;
              break;
            case '+':
              pd[0]='p';
              pd[1]='l';
              pd+=2;
              break;
            case '*':
              pd[0]='s';
              pd[1]='t';
              pd+=2;
              break;
            case '/':
              pd[0]='d';
              pd[1]='i';
              pd+=2;
              break;
            case '<':
              pd[0]='l';
              pd[1]='t';
              pd+=2;
              break;
            case '>':
              pd[0]='g';
              pd[1]='t';
              pd+=2;
              break;
            case '=':
              pd[0]='e';
              pd[1]='q';
              pd+=2;
              break;
            case '?':
              pd[0]='q';
              pd[1]='m';
              pd+=2;
              break;
            case '!':
              pd[0]='e';
              pd[1]='x';
              pd+=2;
              break;
            case '%':
              pd[0]='p';
              pd[1]='c';
              pd+=2;
              break;
            case '~':
              pd[0]='t';
              pd[1]='i';
              pd+=2;
              break;
            case '@':
              pd[0]='a';
              pd[1]='t';
              pd+=2;
              break;
            default:
              if (pd > dupstr && pd[-1] != '_')
                *(pd++) = '_';
              else
                *pd = (char) 0;
            }
        pd[1] = (char) 0;
      }
  meltgc_add_out_raw (outbuf_p, dupstr);
  if (dupstr && dupstr != tinybuf)
    free (dupstr);
}

void
meltgc_add_out_cidentprefix (melt_ptr_t outbuf_p, const char *str, int preflen)
{
  const char *ps = 0;
  char *pd = 0;
  char tinybuf[80];
  if (str)
    {
      int lenst = strlen (str);
      if (lenst < preflen)
        preflen = lenst;
    }
  else
    return;
  /* we don't care to trim the C identifier in generated stuff */
  if (preflen >= (int) sizeof (tinybuf) - 1)
    preflen = sizeof (tinybuf) - 2;
  if (preflen <= 0)
    return;
  memset (tinybuf, 0, sizeof (tinybuf));
  for (pd = tinybuf, ps = str; ps < str + preflen && *ps; ps++)
    {
      if (ISALNUM (*ps))
        *(pd++) = *ps;
      else if (pd > tinybuf && pd[-1] != '_')
        *(pd++) = '_';
    }
  meltgc_add_out_raw (outbuf_p, tinybuf);
}


void
meltgc_add_out_hex (melt_ptr_t outbuf_p, unsigned long l)
{
  if (l == 0UL)
    meltgc_add_out_raw (outbuf_p, "0");
  else
    {
      int ix = 0, j = 0;
      char revbuf[80], thebuf[80];
      memset (revbuf, 0, sizeof (revbuf));
      memset (thebuf, 0, sizeof (thebuf));
      while (ix < (int) sizeof (revbuf) - 1 && l != 0UL)
        {
          unsigned h = l & 15;
          l >>= 4;
          revbuf[ix++] = "0123456789abcdef"[h];
        }
      ix--;
      for (j = 0; j < (int) sizeof (thebuf) - 1 && ix >= 0; j++, ix--)
        thebuf[j] = revbuf[ix];
      meltgc_add_out_raw (outbuf_p, thebuf);
    }
}


void
meltgc_add_out_dec (melt_ptr_t outbuf_p, long l)
{
  if (l == 0L)
    meltgc_add_out_raw (outbuf_p, "0");
  else
    {
      int ix = 0, j = 0, neg = 0;
      char revbuf[96], thebuf[96];
      memset (revbuf, 0, sizeof (revbuf));
      memset (thebuf, 0, sizeof (thebuf));
      if (l < 0)
        {
          l = -l;
          neg = 1;
        };
      while (ix < (int) sizeof (revbuf) - 1 && l != 0UL)
        {
          unsigned h = l % 10;
          l = l / 10;
          revbuf[ix++] = "0123456789"[h];
        }
      ix--;
      if (neg)
        {
          thebuf[0] = '-';
          j = 1;
        };
      for (; j < (int) sizeof (thebuf) - 1 && ix >= 0; j++, ix--)
        thebuf[j] = revbuf[ix];
      meltgc_add_out_raw (outbuf_p, thebuf);
    }
}


void
meltgc_out_printf (melt_ptr_t outbuf_p,
                   const char *fmt, ...)
{
  char *cstr = NULL;
  va_list ap;
  int l = 0;
  char tinybuf[120];
  MELT_ENTERFRAME (2, NULL);
#define outbufv  meltfram__.mcfr_varptr[0]
  outbufv = outbuf_p;
  if (!melt_is_out ((melt_ptr_t) outbufv))
    goto end;
  memset (tinybuf, 0, sizeof (tinybuf));
  va_start (ap, fmt);
  l = vsnprintf (tinybuf, sizeof (tinybuf) - 1, fmt, ap);
  va_end (ap);
  if (l < (int) sizeof (tinybuf) - 3)
    {
      meltgc_add_strbuf_raw ((melt_ptr_t) outbufv, tinybuf);
      goto end;
    }
  va_start (ap, fmt);
  cstr = (char*) xcalloc ((l + 10)|7, 1);
  memset (cstr, 0, l+2);
  if (vsprintf (cstr, fmt, ap) > l)
    gcc_unreachable ();
  va_end (ap);
  meltgc_add_out_raw ((melt_ptr_t) outbufv, cstr);
  free (cstr);
end:
  MELT_EXITFRAME ();
#undef outbufv
}


/* add safely into OUTBUF either a space or an indented newline if the current line is bigger than the threshold */
void
meltgc_out_add_indent (melt_ptr_t outbuf_p, int depth, int linethresh)
{
  int llln = 0;     /* last line length */
  int outmagic = 0;   /* the magic of outbuf */
  MELT_ENTERFRAME (2, NULL);
  /* we need a frame, because we have more than one call to
     meltgc_add_outbuf_raw */
#define outbv   meltfram__.mcfr_varptr[0]
#define outbufv ((struct meltstrbuf_st*)(outbv))
#define spec_outv ((struct meltspecial_st*)(outbv))
#define spda_outv ((struct meltspecialdata_st*)(outbv))
  outbv = outbuf_p;
  if (!outbv)
    goto end;
  outmagic = melt_magic_discr((melt_ptr_t) outbv);
  if (linethresh > 0 && linethresh < 40)
    linethresh = 40;
  /* compute the last line length llln */
  if (outmagic == MELTOBMAG_STRBUF)
    {
      char *bs = 0, *be = 0, *nl = 0;
      bs = outbufv->bufzn + outbufv->bufstart;
      be = outbufv->bufzn + outbufv->bufend;
      for (nl = be - 1; nl > bs && *nl && *nl != '\n'; nl--);
      llln = be - nl;
      gcc_assert (llln >= 0);
    }
  else if (outmagic == MELTOBMAG_SPECIAL_DATA)
    {
      FILE *f = spda_outv->meltspec_payload.meltpayload_file1;
      int fn = f?fileno(f):-1;
      if (f && fn>=0 && fn<=MELTMAXFILE)
        llln = ftell(f) - lasteol[fn];
    }
  if (linethresh > 0 && llln < linethresh)
    meltgc_add_out_raw ((melt_ptr_t) outbv, " ");
  else
    {
      int nbsp = depth;
      static const char spaces32[] = "                                ";
      meltgc_add_out_raw ((melt_ptr_t) outbv, "\n");
      if (nbsp < 0)
        nbsp = 0;
      if (nbsp > 0 && nbsp % 32 != 0)
        meltgc_add_out_raw ((melt_ptr_t) outbv, spaces32 + (32 - nbsp % 32));
    }
end:
  MELT_EXITFRAME ();
#undef outbufv
#undef outbv
#undef spec_outv
#undef spda_outv
}


void
melt_output_strbuf_to_file (melt_ptr_t sbuf, const char*filnam)
{
  FILE* fil=0;
  char* namdot=0;
  char tmpsuffix[64];
  time_t now = 0;
  /* we don't have any MELT garbage collection roots, because no
     allocation is done! */
  if (!sbuf || melt_magic_discr (sbuf) != MELTOBMAG_STRBUF)
    return;
  if (!filnam || !filnam[0])
    return;
  /* Use a unique temporary suffix to be more friendly when GCC MELT
     is invoked by a parallel make.  */
  memset (tmpsuffix, 0, sizeof(tmpsuffix));
  time (&now);
  snprintf (tmpsuffix, sizeof(tmpsuffix)-1, ".%d-%d-%d.tmp",
            (int) getpid(), ((int) now) % 1000,
            (int) ((melt_lrand()) & 0xfffff));
  namdot = concat(filnam, tmpsuffix, NULL);
  fil = fopen(namdot, "w");
  if (!fil)
    melt_fatal_error ("failed to open MELT output file %s [%s]",
                      namdot, xstrerror (errno));
  if (fwrite (melt_strbuf_str (sbuf), (size_t) melt_strbuf_usedlength (sbuf),
              (size_t) 1, fil) <= 0)
    melt_fatal_error ("failed to write %d bytes into MELT output file %s [%s]",
                      melt_strbuf_usedlength (sbuf), namdot, xstrerror (errno));
  if (fclose (fil))
    melt_fatal_error ("failed to close MELT output file %s [%s]",
                      namdot, xstrerror (errno));
  fil = NULL;
  if (rename (namdot, filnam))
    melt_fatal_error ("failed to rename MELT output file from %s to %s [%s]",
                      namdot, filnam, xstrerror (errno));
  free (namdot);
}



/***************/



meltobject_ptr_t
meltgc_new_raw_object (meltobject_ptr_t klass_p, unsigned len)
{
  unsigned h = 0;
  MELT_ENTERFRAME (2, NULL);
#define newobjv   meltfram__.mcfr_varptr[0]
#define klassv    meltfram__.mcfr_varptr[1]
#define obj_newobjv  ((meltobject_ptr_t)(newobjv))
#define obj_klassv   ((meltobject_ptr_t)(klassv))
  newobjv = NULL;
  klassv = (melt_ptr_t) klass_p;
  if (melt_magic_discr ((melt_ptr_t) (klassv)) != MELTOBMAG_OBJECT
      || obj_klassv->meltobj_magic != MELTOBMAG_OBJECT || len >= SHRT_MAX)
    goto end;
  /* the sizeof below could be the offsetof obj__tabfields */
  newobjv =
    (melt_ptr_t) meltgc_allocate (sizeof (struct meltobject_st),
                                  len * sizeof (void *));
  obj_newobjv->meltobj_class = (meltobject_ptr_t) klassv;
  do
    {
      h = melt_lrand () & MELT_MAXHASH;
    }
  while (h == 0);
  obj_newobjv->obj_hash = h;
  obj_newobjv->obj_len = len;
#if MELT_HAVE_RUNTIME_DEBUG > 0
  if (melt_alptr_1 && (void*)melt_alptr_1 == (void*)newobjv)
    melt_break_alptr_1("newrawobj alptr_1");
  if (melt_alptr_2 && (void*)melt_alptr_2 == (void*)newobjv)
    melt_break_alptr_2("newrawobj alptr_1");
  if (melt_objhash_1 == h)
    melt_break_objhash_1("newrawobj objhash1");
  if (melt_objhash_2 == h)
    melt_break_objhash_1("newrawobj objhash2");
#endif
end:
  MELT_EXITFRAME ();
  return (meltobject_ptr_t) newobjv;
#undef newobjv
#undef klassv
#undef obj_newobjv
#undef obj_klassv
}


/* allocate a new multiple of given DISCR & length LEN */
melt_ptr_t
meltgc_new_multiple (meltobject_ptr_t discr_p, unsigned len)
{
  MELT_ENTERFRAME (2, NULL);
#define newmul meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mult_newmul ((struct meltmultiple_st*)(newmul))
  discrv = (melt_ptr_t) discr_p;
  newmul = NULL;
  gcc_assert (len < MELT_MAXLEN);
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MULTIPLE)
    goto end;
  newmul =
    (melt_ptr_t) meltgc_allocate (sizeof (struct meltmultiple_st),
                                  sizeof (void *) * len);
  mult_newmul->discr = object_discrv;
  mult_newmul->nbval = len;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmul;
#undef newmul
#undef discrv
#undef mult_newmul
#undef object_discrv
}

/* make a subsequence of a given multiple OLDMUL_P from STARTIX to
   ENDIX; if either index is negative, take it from last.  return null
   if arguments are incorrect, or a fresh subsequence of same
   discriminant as source otherwise */
melt_ptr_t
meltgc_new_subseq_multiple (melt_ptr_t oldmul_p, int startix, int endix)
{
  int oldlen=0, newlen=0, i=0;
  MELT_ENTERFRAME(3, NULL);
#define oldmulv   meltfram__.mcfr_varptr[0]
#define newmulv   meltfram__.mcfr_varptr[1]
#define mult_oldmulv   ((struct meltmultiple_st*)(oldmulv))
#define mult_newmulv   ((struct meltmultiple_st*)(newmulv))
  oldmulv = oldmul_p;
  newmulv = NULL;
  if (melt_magic_discr ((melt_ptr_t) (oldmulv)) != MELTOBMAG_MULTIPLE)
    goto end;
  oldlen = mult_oldmulv->nbval;
  if (startix < 0)
    startix += oldlen;
  if (endix < 0)
    endix += oldlen;
  if (startix < 0 || startix >= oldlen)
    goto end;
  if (endix < 0 || endix >= oldlen || endix < startix)
    goto end;
  newlen = endix - startix;
  newmulv =
    (melt_ptr_t) meltgc_allocate (sizeof (struct meltmultiple_st),
                                  sizeof (void *) * newlen);
  mult_newmulv->discr = mult_oldmulv->discr;
  mult_newmulv->nbval = newlen;
  for (i=0; i<newlen; i++)
    mult_newmulv->tabval[i] = mult_oldmulv->tabval[startix+i];
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmulv;
#undef oldmulv
#undef newmulv
#undef mult_oldmulv
#undef mult_newmulv
}

void
meltgc_multiple_put_nth (melt_ptr_t mul_p, int n, melt_ptr_t val_p)
{
  int ln = 0;
  MELT_ENTERFRAME (2, NULL);
#define mulv    meltfram__.mcfr_varptr[0]
#define mult_mulv ((struct meltmultiple_st*)(mulv))
#define valv    meltfram__.mcfr_varptr[1]
  mulv = mul_p;
  valv = val_p;
  if (melt_magic_discr ((melt_ptr_t) (mulv)) != MELTOBMAG_MULTIPLE)
    goto end;
  ln = mult_mulv->nbval;
  if (n < 0)
    n += ln;
  if (n >= 0 && n < ln)
    {
      mult_mulv->tabval[n] = (melt_ptr_t) valv;
      meltgc_touch_dest (mulv, valv);
    }
end:
  MELT_EXITFRAME ();
#undef mulv
#undef mult_mulv
#undef valv
}





/* safely return the content of a reference - instance of CLASS_REFERENCE */
melt_ptr_t
melt_reference_value (melt_ptr_t cont)
{
  if (melt_magic_discr (cont) != MELTOBMAG_OBJECT
      || ((meltobject_ptr_t) cont)->obj_len < MELTLENGTH_CLASS_REFERENCE)
    return NULL;
  /* This case is so common that we handle it explicitly! */
  if (((meltobject_ptr_t)cont)->discr
      == (meltobject_ptr_t)MELT_PREDEF (CLASS_REFERENCE))
    return  ((meltobject_ptr_t) cont)->obj_vartab[MELTFIELD_REFERENCED_VALUE];
  if (!melt_is_instance_of
      ((melt_ptr_t) cont, (melt_ptr_t) MELT_PREDEF (CLASS_REFERENCE)))
    return NULL;
  return ((meltobject_ptr_t) cont)->obj_vartab[MELTFIELD_REFERENCED_VALUE];
}


/* make a new reference */
melt_ptr_t
meltgc_new_reference (melt_ptr_t val_p)
{
  MELT_ENTERFRAME(3, NULL);
#define valv        meltfram__.mcfr_varptr[0]
#define resv        meltfram__.mcfr_varptr[1]
#define classrefv  meltfram__.mcfr_varptr[2]
  valv = val_p;
  classrefv = MELT_PREDEF (CLASS_REFERENCE);
  gcc_assert (melt_magic_discr ((melt_ptr_t)classrefv) == MELTOBMAG_OBJECT);
  /* we really need that references have one single field */
  gcc_assert (MELTFIELD_REFERENCED_VALUE == 0);
  gcc_assert (MELTLENGTH_CLASS_REFERENCE == 1);
  resv = (melt_ptr_t) meltgc_new_raw_object ((meltobject_ptr_t) classrefv,
         MELTLENGTH_CLASS_REFERENCE);
  ((meltobject_ptr_t) (resv))->obj_vartab[MELTFIELD_REFERENCED_VALUE] =
    (melt_ptr_t) valv;
  MELT_EXITFRAME();
  return (melt_ptr_t)resv;
#undef valv
#undef resv
#undef classrefv
}

/* put inside a reference */
void
meltgc_reference_put (melt_ptr_t ref_p, melt_ptr_t val_p)
{
  MELT_ENTERFRAME(3, NULL);
#define refv    meltfram__.mcfr_varptr[0]
#define valv     meltfram__.mcfr_varptr[1]
#define classrefv  meltfram__.mcfr_varptr[2]
  refv = (melt_ptr_t) ref_p;
  valv = (melt_ptr_t)val_p;
  classrefv = (melt_ptr_t) MELT_PREDEF (CLASS_REFERENCE);
  gcc_assert (melt_magic_discr ((melt_ptr_t)classrefv) == MELTOBMAG_OBJECT);
  /* we really need that references have one single field */
  gcc_assert (MELTFIELD_REFERENCED_VALUE == 0);
  if (melt_magic_discr((melt_ptr_t)refv) != MELTOBMAG_OBJECT)
    goto end;
  /* This case is so common that we handle it explicitly! */
  if ((melt_ptr_t) ((meltobject_ptr_t)refv)->discr != (melt_ptr_t) classrefv
      && !melt_is_instance_of
      ((melt_ptr_t) refv, (melt_ptr_t) classrefv))
    goto end;
  ((meltobject_ptr_t) (refv))->obj_vartab[MELTFIELD_REFERENCED_VALUE] =
    (melt_ptr_t) valv;
  meltgc_touch_dest (refv, valv);
end:
  MELT_EXITFRAME();
#undef valv
#undef refv
#undef classrefv
}

/****** MULTIPLES ******/


melt_ptr_t
meltgc_new_list (meltobject_ptr_t discr_p)
{
  MELT_ENTERFRAME (2, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define newlist meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define list_newlist ((struct meltlist_st*)(newlist))
  discrv = (melt_ptr_t) discr_p;
  newlist = NULL;
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_LIST)
    goto end;
  newlist = (melt_ptr_t) meltgc_allocate (sizeof (struct meltlist_st), 0);
  list_newlist->discr = object_discrv;
  list_newlist->first = NULL;
  list_newlist->last = NULL;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newlist;
#undef newlist
#undef discrv
#undef list_newlist
#undef object_discrv
}

melt_ptr_t
meltgc_new_list_from_pair (meltobject_ptr_t discr_p, melt_ptr_t pair_p)
{
  MELT_ENTERFRAME (5, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define newlist meltfram__.mcfr_varptr[1]
#define pairv meltfram__.mcfr_varptr[2]
#define firstpairv meltfram__.mcfr_varptr[3]
#define lastpairv meltfram__.mcfr_varptr[4]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define list_newlist ((struct meltlist_st*)(newlist))
  discrv = (melt_ptr_t) discr_p;
  pairv = (melt_ptr_t) pair_p;
  newlist = NULL;
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_LIST)
    goto end;
  if (melt_magic_discr((melt_ptr_t) pairv) == MELTOBMAG_PAIR)
    {
      firstpairv = pairv;
      lastpairv = firstpairv;
      while (melt_magic_discr((melt_ptr_t) lastpairv) == MELTOBMAG_PAIR
             && (((struct meltpair_st *)lastpairv)->tl) != NULL)
        lastpairv = (melt_ptr_t)(((struct meltpair_st *)lastpairv)->tl);
    }
  newlist = (melt_ptr_t) meltgc_allocate (sizeof (struct meltlist_st), 0);
  list_newlist->discr = object_discrv;
  list_newlist->first = (struct meltpair_st*)firstpairv;
  list_newlist->last = (struct meltpair_st*)lastpairv;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newlist;
#undef newlist
#undef discrv
#undef firstpair
#undef lastpair
#undef pairv
#undef list_newlist
#undef object_discrv
}

/* allocate a pair of given head and tail */
melt_ptr_t
meltgc_new_pair (meltobject_ptr_t discr_p, void *head_p, void *tail_p)
{
  MELT_ENTERFRAME (4, NULL);
#define pairv   meltfram__.mcfr_varptr[0]
#define discrv  meltfram__.mcfr_varptr[1]
#define headv   meltfram__.mcfr_varptr[2]
#define tailv   meltfram__.mcfr_varptr[3]
  discrv = (melt_ptr_t) discr_p;
  headv = (melt_ptr_t) head_p;
  tailv = (melt_ptr_t) tail_p;
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT
      || ((meltobject_ptr_t) (discrv))->meltobj_magic != MELTOBMAG_PAIR)
    goto end;
  if (melt_magic_discr ((melt_ptr_t) tailv) != MELTOBMAG_PAIR)
    tailv = NULL;
  pairv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltpair_st), 0);
  ((struct meltpair_st *) (pairv))->discr = (meltobject_ptr_t) discrv;
  ((struct meltpair_st *) (pairv))->hd = (melt_ptr_t) headv;
  ((struct meltpair_st *) (pairv))->tl = (struct meltpair_st *) tailv;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) pairv;
#undef pairv
#undef headv
#undef tailv
#undef discrv
}

/* change the head of a pair */
void
meltgc_pair_set_head (melt_ptr_t pair_p, void *head_p)
{
  MELT_ENTERFRAME (2, NULL);
#define pairv   meltfram__.mcfr_varptr[0]
#define headv   meltfram__.mcfr_varptr[1]
  pairv = (melt_ptr_t) pair_p;
  headv = (melt_ptr_t) head_p;
  if (melt_magic_discr ((melt_ptr_t) pairv) != MELTOBMAG_PAIR)
    goto end;
  ((struct meltpair_st *) pairv)->hd = (melt_ptr_t) headv;
  meltgc_touch_dest (pairv, headv);
end:
  MELT_EXITFRAME ();
#undef pairv
#undef headv
}


void
meltgc_append_list (melt_ptr_t list_p, melt_ptr_t valu_p)
{
  MELT_ENTERFRAME (4, NULL);
#define list meltfram__.mcfr_varptr[0]
#define valu meltfram__.mcfr_varptr[1]
#define pairv meltfram__.mcfr_varptr[2]
#define lastv meltfram__.mcfr_varptr[3]
#define pai_pairv ((struct meltpair_st*)(pairv))
#define list_list ((struct meltlist_st*)(list))
  list = list_p;
  valu = valu_p;
  if (melt_magic_discr ((melt_ptr_t) list) != MELTOBMAG_LIST
      || ! MELT_PREDEF (DISCR_PAIR))
    goto end;
  pairv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltpair_st), 0);
  pai_pairv->discr = (meltobject_ptr_t) MELT_PREDEF (DISCR_PAIR);
  pai_pairv->hd = (melt_ptr_t) valu;
  pai_pairv->tl = NULL;
  gcc_assert (melt_magic_discr ((melt_ptr_t) pairv) == MELTOBMAG_PAIR);
  lastv = (melt_ptr_t) list_list->last;
  if (melt_magic_discr ((melt_ptr_t) lastv) == MELTOBMAG_PAIR)
    {
      gcc_assert (((struct meltpair_st *) lastv)->tl == NULL);
      ((struct meltpair_st *) lastv)->tl = (struct meltpair_st *) pairv;
      meltgc_touch_dest (lastv, pairv);
    }
  else
    list_list->first = (struct meltpair_st *) pairv;
  list_list->last = (struct meltpair_st *) pairv;
  meltgc_touch_dest (list, pairv);
end:
  MELT_EXITFRAME ();
#undef list
#undef valu
#undef list_list
#undef pairv
#undef pai_pairv
#undef lastv
}

void
meltgc_prepend_list (melt_ptr_t list_p, melt_ptr_t valu_p)
{
  MELT_ENTERFRAME (4, NULL);
#define list meltfram__.mcfr_varptr[0]
#define valu meltfram__.mcfr_varptr[1]
#define pairv meltfram__.mcfr_varptr[2]
#define firstv meltfram__.mcfr_varptr[3]
#define pai_pairv ((struct meltpair_st*)(pairv))
#define list_list ((struct meltlist_st*)(list))
  list = list_p;
  valu = valu_p;
  if (melt_magic_discr ((melt_ptr_t) list) != MELTOBMAG_LIST
      || ! MELT_PREDEF (DISCR_PAIR))
    goto end;
  pairv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltpair_st), 0);
  pai_pairv->discr = (meltobject_ptr_t) MELT_PREDEF (DISCR_PAIR);
  pai_pairv->hd = (melt_ptr_t) valu;
  pai_pairv->tl = NULL;
  gcc_assert (melt_magic_discr ((melt_ptr_t) pairv) == MELTOBMAG_PAIR);
  firstv = (melt_ptr_t) (list_list->first);
  if (melt_magic_discr ((melt_ptr_t) firstv) == MELTOBMAG_PAIR)
    {
      pai_pairv->tl = (struct meltpair_st *) firstv;
      meltgc_touch_dest (pairv, firstv);
    }
  else
    list_list->last = (struct meltpair_st *) pairv;
  list_list->first = (struct meltpair_st *) pairv;
  meltgc_touch_dest (list, pairv);
end:
  MELT_EXITFRAME ();
#undef list
#undef valu
#undef list_list
#undef pairv
#undef pai_pairv
}


melt_ptr_t
meltgc_popfirst_list (melt_ptr_t list_p)
{
  MELT_ENTERFRAME (3, NULL);
#define list meltfram__.mcfr_varptr[0]
#define valu meltfram__.mcfr_varptr[1]
#define pairv meltfram__.mcfr_varptr[2]
#define pai_pairv ((struct meltpair_st*)(pairv))
#define list_list ((struct meltlist_st*)(list))
  list = list_p;
  if (melt_magic_discr ((melt_ptr_t) list) != MELTOBMAG_LIST)
    goto end;
  pairv = (melt_ptr_t) list_list->first;
  if (melt_magic_discr ((melt_ptr_t) pairv) != MELTOBMAG_PAIR)
    goto end;
  if ((melt_ptr_t) list_list->last == (melt_ptr_t) pairv)
    {
      valu = pai_pairv->hd;
      list_list->first = NULL;
      list_list->last = NULL;
    }
  else
    {
      valu = pai_pairv->hd;
      list_list->first = pai_pairv->tl;
    }
  meltgc_touch (list);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) valu;
#undef list
#undef value
#undef list_list
#undef pairv
#undef pai_pairv
}       /* enf of popfirst */


/* return the length of a list or -1 iff non list */
int
melt_list_length (melt_ptr_t list_p)
{
  struct meltpair_st *pair = NULL;
  int ln = 0;
  if (!list_p)
    return 0;
  if (melt_magic_discr (list_p) != MELTOBMAG_LIST)
    return -1;
  for (pair = ((struct meltlist_st *) list_p)->first;
       melt_magic_discr ((melt_ptr_t) pair) ==
       MELTOBMAG_PAIR;
       pair = (struct meltpair_st *) (pair->tl))
    ln++;
  return ln;
}


/* allocate a new empty mapobjects */
melt_ptr_t
meltgc_new_mapobjects (meltobject_ptr_t discr_p, unsigned len)
{
  int maplen = 0;
  int lenix = 0, primlen = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define newmapv meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mapobject_newmapv ((struct meltmapobjects_st*)(newmapv))
  discrv = (melt_ptr_t) discr_p;
  if (!discrv || object_discrv->meltobj_class->meltobj_magic != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MAPOBJECTS)
    goto end;
  if (len > 0)
    {
      gcc_assert (len < (unsigned) MELT_MAXLEN);
      for (lenix = 1;
           (primlen = (int) melt_primtab[lenix]) != 0
           && primlen <= (int) len; lenix++);
      maplen = primlen;
    };
  meltgc_reserve (sizeof(struct meltmapobjects_st)
                  + maplen * sizeof (struct entryobjectsmelt_st)
                  + 8 * sizeof(void*));
  newmapv = (melt_ptr_t)
            meltgc_allocate (offsetof
                             (struct meltmapobjects_st, map_space),
                             maplen * sizeof (struct entryobjectsmelt_st));
  mapobject_newmapv->discr = object_discrv;
  mapobject_newmapv->meltmap_aux = NULL;
  mapobject_newmapv->meltmap_hash = melt_nonzerohash ();
  if (len > 0)
    {
      mapobject_newmapv->entab = mapobject_newmapv->map_space;
      mapobject_newmapv->lenix = lenix;
    };
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmapv;
#undef discrv
#undef newmapv
#undef object_discrv
#undef mapobject_newmapv
}

/* get from a mapobject */
melt_ptr_t
melt_get_mapobjects (meltmapobjects_ptr_t mapobject_p,
                     meltobject_ptr_t attrobject_p)
{
  long ix, len;
  melt_ptr_t val = NULL;
  if (!mapobject_p || !attrobject_p
      || mapobject_p->discr->meltobj_magic != MELTOBMAG_MAPOBJECTS
      || !mapobject_p->entab
      || attrobject_p->meltobj_class->meltobj_magic != MELTOBMAG_OBJECT)
    return NULL;
  len = melt_primtab[mapobject_p->lenix];
  if (len <= 0)
    return NULL;
  ix = unsafe_index_mapobject (mapobject_p->entab, attrobject_p, len);
  if (ix < 0)
    return NULL;
  if (mapobject_p->entab[ix].e_at == attrobject_p)
    val = mapobject_p->entab[ix].e_va;
  return val;
}

void
meltgc_put_mapobjects (meltmapobjects_ptr_t
                       mapobject_p,
                       meltobject_ptr_t attrobject_p,
                       melt_ptr_t valu_p)
{
  long ix = 0, len = 0, cnt = 0;
  MELT_ENTERFRAME (4, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define mapobjectv meltfram__.mcfr_varptr[1]
#define attrobjectv meltfram__.mcfr_varptr[2]
#define valuv meltfram__.mcfr_varptr[3]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define object_attrobjectv ((meltobject_ptr_t)(attrobjectv))
#define map_mapobjectv ((meltmapobjects_ptr_t)(mapobjectv))
  mapobjectv = (melt_ptr_t) mapobject_p;
  attrobjectv = (melt_ptr_t) attrobject_p;
  valuv = (melt_ptr_t) valu_p;
  if (!mapobjectv || !attrobjectv || !valuv)
    goto end;
  discrv = (melt_ptr_t) map_mapobjectv->discr;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_MAPOBJECTS)
    goto end;
  discrv = (melt_ptr_t) object_attrobjectv->meltobj_class;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_OBJECT)
    goto end;
  if (!map_mapobjectv->entab)
    {
      /* fresh map without any entab; allocate it minimally */
      size_t lensiz = 0;
      len = melt_primtab[1];  /* i.e. 3 */
      lensiz = len * sizeof (struct entryobjectsmelt_st);
      if (melt_is_young (mapobjectv))
        {
          meltgc_reserve (lensiz + 20);
          if (!melt_is_young (mapobjectv))
            goto alloc_old_smallmapobj;
          map_mapobjectv->entab =
            (struct entryobjectsmelt_st *)
            melt_allocatereserved (lensiz, 0);
        }
      else
        {
alloc_old_smallmapobj:
          map_mapobjectv->entab = ggc_alloc_cleared_vec_entryobjectsmelt_st (len);
        }
      map_mapobjectv->lenix = 1;
      meltgc_touch (map_mapobjectv);
    }
  else if ((len = melt_primtab[map_mapobjectv->lenix]) <=
           (5 * (cnt = map_mapobjectv->count)) / 4
           || (len <= 5 && cnt + 1 >= len))
    {
      /* entab is nearly full so need to be resized */
      int ix, newcnt = 0;
      int newlen = melt_primtab[map_mapobjectv->lenix + 1];
      size_t newlensiz = 0;
      struct entryobjectsmelt_st *newtab = NULL;
      struct entryobjectsmelt_st *oldtab = NULL;
      newlensiz = newlen * sizeof (struct entryobjectsmelt_st);
      if (melt_is_young (map_mapobjectv->entab))
        {
          meltgc_reserve (newlensiz + 100);
          if (!melt_is_young (map_mapobjectv))
            goto alloc_old_mapobj;
          newtab =
            (struct entryobjectsmelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
alloc_old_mapobj:
          newtab = ggc_alloc_cleared_vec_entryobjectsmelt_st (newlen);
        };
      oldtab = map_mapobjectv->entab;
      for (ix = 0; ix < len; ix++)
        {
          meltobject_ptr_t curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mapobject (newtab, curat, newlen);
          gcc_assert (newix >= 0);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in old ggc space */
        ggc_free (oldtab);
      map_mapobjectv->entab = newtab;
      map_mapobjectv->count = newcnt;
      map_mapobjectv->lenix++;
      meltgc_touch (map_mapobjectv);
      len = newlen;
    }
  ix =
    unsafe_index_mapobject (map_mapobjectv->entab, object_attrobjectv, len);
  gcc_assert (ix >= 0);
  if ((melt_ptr_t) map_mapobjectv->entab[ix].e_at != (melt_ptr_t) attrobjectv)
    {
      map_mapobjectv->entab[ix].e_at = (meltobject_ptr_t) attrobjectv;
      map_mapobjectv->count++;
    }
  map_mapobjectv->entab[ix].e_va = (melt_ptr_t) valuv;
  meltgc_touch_dest (map_mapobjectv, attrobjectv);
  meltgc_touch_dest (map_mapobjectv, valuv);
end:
  MELT_EXITFRAME ();
#undef discrv
#undef mapobjectv
#undef attrobjectv
#undef valuv
#undef object_discrv
#undef object_attrobjectv
#undef map_mapobjectv
}


melt_ptr_t
meltgc_remove_mapobjects (meltmapobjects_ptr_t
                          mapobject_p, meltobject_ptr_t attrobject_p)
{
  long ix = 0, len = 0, cnt = 0;
  MELT_ENTERFRAME (4, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define mapobjectv meltfram__.mcfr_varptr[1]
#define attrobjectv meltfram__.mcfr_varptr[2]
#define valuv meltfram__.mcfr_varptr[3]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define object_attrobjectv ((meltobject_ptr_t)(attrobjectv))
#define map_mapobjectv ((meltmapobjects_ptr_t)(mapobjectv))
  mapobjectv = (melt_ptr_t) mapobject_p;
  attrobjectv = (melt_ptr_t) attrobject_p;
  valuv = NULL;
  if (!mapobjectv || !attrobjectv)
    goto end;
  discrv = (melt_ptr_t) map_mapobjectv->discr;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_MAPOBJECTS)
    goto end;
  discrv = (melt_ptr_t) object_attrobjectv->meltobj_class;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_OBJECT)
    goto end;
  if (!map_mapobjectv->entab)
    goto end;
  len = melt_primtab[map_mapobjectv->lenix];
  if (len <= 0)
    goto end;
  ix = unsafe_index_mapobject (map_mapobjectv->entab, attrobject_p, len);
  if (ix < 0 || (melt_ptr_t) map_mapobjectv->entab[ix].e_at != (melt_ptr_t) attrobjectv)
    goto end;
  map_mapobjectv->entab[ix].e_at = (meltobject_ptr_t) HTAB_DELETED_ENTRY;
  valuv = map_mapobjectv->entab[ix].e_va;
  map_mapobjectv->entab[ix].e_va = NULL;
  map_mapobjectv->count--;
  cnt = map_mapobjectv->count;
  if (len >= 7 && cnt < len / 2 - 2)
    {
      int newcnt = 0, newlen = 0, newlenix = 0;
      size_t newlensiz = 0;
      struct entryobjectsmelt_st *oldtab = NULL, *newtab = NULL;
      for (newlenix = map_mapobjectv->lenix;
           (newlen = melt_primtab[newlenix]) > 2 * cnt + 3; newlenix--);
      if (newlen >= len)
        goto end;
      newlensiz = newlen * sizeof (struct entryobjectsmelt_st);
      if (melt_is_young (map_mapobjectv->entab))
        {
          /* reserve a zone; if a GC occurred, the mapobject & entab
             could become old */
          meltgc_reserve (newlensiz + 10 * sizeof (void *));
          if (!melt_is_young (map_mapobjectv))
            goto alloc_old_entries;
          newtab =
            (struct entryobjectsmelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
alloc_old_entries:
          newtab = ggc_alloc_cleared_vec_entryobjectsmelt_st (newlen);
        }
      oldtab = map_mapobjectv->entab;
      for (ix = 0; ix < len; ix++)
        {
          meltobject_ptr_t curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mapobject (newtab, curat, newlen);
          gcc_assert (newix >= 0);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in old ggc space */
        ggc_free (oldtab);
      map_mapobjectv->entab = newtab;
      map_mapobjectv->count = newcnt;
      map_mapobjectv->lenix = newlenix;
    }
  meltgc_touch (map_mapobjectv);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) valuv;
#undef discrv
#undef mapobjectv
#undef attrobjectv
#undef valuv
#undef object_discrv
#undef object_attrobjectv
#undef map_mapobjectv
}



/* index of entry to get or add an attribute in an mapstring (or -1 on error) */
static inline int
unsafe_index_mapstring (struct entrystringsmelt_st *tab,
                        const char *attr, int siz)
{
  int ix = 0, frix = -1;
  unsigned h = 0;
  if (!tab || !attr || siz <= 0)
    return -1;
  h = (unsigned) htab_hash_string (attr) & MELT_MAXHASH;
  h = h % siz;
  for (ix = h; ix < siz; ix++)
    {
      const char *curat = tab[ix].e_at;
      if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
      else if (!strcmp (curat, attr))
        return ix;
    }
  for (ix = 0; ix < (int) h; ix++)
    {
      const char *curat = tab[ix].e_at;
      if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
      else if (!strcmp (curat, attr))
        return ix;
    }
  if (frix >= 0)    /* found a place in a table with deleted entries */
    return frix;
  return -1;      /* entirely full, should not happen */
}

/* allocate a new empty mapstrings */
melt_ptr_t
meltgc_new_mapstrings (meltobject_ptr_t discr_p, unsigned len)
{
  int lenix = -1, primlen = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define newmapv meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define mapstring_newmapv ((struct meltmapstrings_st*)(newmapv))
  discrv = (melt_ptr_t) discr_p;
  if (!discrv || object_discrv->meltobj_class->meltobj_magic != MELTOBMAG_OBJECT)
    goto end;
  if (object_discrv->meltobj_magic != MELTOBMAG_MAPSTRINGS)
    goto end;
  if (len > 0)
    {
      gcc_assert (len < (unsigned) MELT_MAXLEN);
      for (lenix = 1;
           (primlen = (int) melt_primtab[lenix]) != 0
           && primlen <= (int) len; lenix++);
    };
  gcc_assert (primlen > (int) len);
  meltgc_reserve (sizeof (struct meltmapstrings_st)
                  + primlen * sizeof (struct entrystringsmelt_st)
                  + 8 * sizeof(void*));
  newmapv = (melt_ptr_t)
            meltgc_allocate (sizeof (struct meltmapstrings_st), 0);
  mapstring_newmapv->discr = object_discrv;
  mapstring_newmapv->meltmap_aux = NULL;
  mapstring_newmapv->meltmap_hash = melt_nonzerohash ();
  mapstring_newmapv->count = 0;
  if (len > 0)
    {
      /* the newmapv is always young */
      mapstring_newmapv->entab = (struct entrystringsmelt_st *)
                                 meltgc_allocate (primlen *
                                     sizeof (struct entrystringsmelt_st), 0);
      mapstring_newmapv->lenix = lenix;
      meltgc_touch_dest (newmapv, mapstring_newmapv->entab);
    }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) newmapv;
#undef discrv
#undef newmapv
#undef object_discrv
#undef mapstring_newmapv
}


void
meltgc_put_mapstrings (struct meltmapstrings_st *mapstring_p,
                       const char *attr,
                       melt_ptr_t valu_p)
{
  long ix = 0, len = 0, cnt = 0, atlen = 0;
  char *attrdup = 0;
  char tinybuf[130];
  MELT_ENTERFRAME (3, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define mapstringv meltfram__.mcfr_varptr[1]
#define valuv meltfram__.mcfr_varptr[2]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define map_mapstringv ((struct meltmapstrings_st*)(mapstringv))
  mapstringv = (melt_ptr_t) mapstring_p;
  valuv = valu_p;
  if (!mapstringv || !attr || !attr[0] || !valuv)
    goto end;
  discrv = (melt_ptr_t) map_mapstringv->discr;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_MAPSTRINGS)
    goto end;
  atlen = strlen (attr);
  if (atlen < (int) sizeof (tinybuf) - 1)
    {
      memset (tinybuf, 0, sizeof (tinybuf));
      attrdup = strcpy (tinybuf, attr);
    }
  else
    attrdup = strcpy ((char *) xcalloc (atlen + 1, 1), attr);
  if (!map_mapstringv->entab)
    {
      size_t lensiz = 0;
      len = melt_primtab[1];  /* i.e. 3 */
      lensiz = len * sizeof (struct entrystringsmelt_st);
      if (melt_is_young (mapstringv))
        {
          meltgc_reserve (lensiz + 16 * sizeof (void *) + atlen);
          if (!melt_is_young (mapstringv))
            goto alloc_old_small_mapstring;
          map_mapstringv->entab =
            (struct entrystringsmelt_st *)
            melt_allocatereserved (lensiz, 0);
        }
      else
        {
alloc_old_small_mapstring:
          map_mapstringv->entab = ggc_alloc_cleared_vec_entrystringsmelt_st (len);
        }
      map_mapstringv->lenix = 1;
      meltgc_touch (map_mapstringv);
    }
  else if ((len = melt_primtab[map_mapstringv->lenix]) <=
           (5 * (cnt = map_mapstringv->count)) / 4
           || (len <= 5 && cnt + 1 >= len))
    {
      int ix, newcnt = 0;
      int newlen = melt_primtab[map_mapstringv->lenix + 1];
      struct entrystringsmelt_st *oldtab = NULL;
      struct entrystringsmelt_st *newtab = NULL;
      size_t newlensiz = newlen * sizeof (struct entrystringsmelt_st);
      if (melt_is_young (mapstringv))
        {
          meltgc_reserve (newlensiz + 10 * sizeof (void *) + atlen);
          if (!melt_is_young (mapstringv))
            goto alloc_old_mapstring;
          newtab =
            (struct entrystringsmelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
alloc_old_mapstring:
          newtab = ggc_alloc_cleared_vec_entrystringsmelt_st (newlen);
        };
      oldtab = map_mapstringv->entab;
      for (ix = 0; ix < len; ix++)
        {
          const char *curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mapstring (newtab, curat, newlen);
          gcc_assert (newix >= 0);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in old ggc space */
        ggc_free (oldtab);
      map_mapstringv->entab = newtab;
      map_mapstringv->count = newcnt;
      map_mapstringv->lenix++;
      meltgc_touch (map_mapstringv);
      len = newlen;
    }
  ix = unsafe_index_mapstring (map_mapstringv->entab, attrdup, len);
  gcc_assert (ix >= 0);
  if (!map_mapstringv->entab[ix].e_at
      || map_mapstringv->entab[ix].e_at == HTAB_DELETED_ENTRY)
    {
      char *newat = (char *) meltgc_allocate (atlen + 1, 0);
      strcpy (newat, attrdup);
      map_mapstringv->entab[ix].e_at = newat;
      map_mapstringv->count++;
    }
  map_mapstringv->entab[ix].e_va = (melt_ptr_t) valuv;
  meltgc_touch_dest (map_mapstringv, valuv);
end:
  if (attrdup && attrdup != tinybuf)
    free (attrdup);
  MELT_EXITFRAME ();
#undef discrv
#undef mapstringv
#undef attrobjectv
#undef valuv
#undef object_discrv
#undef object_attrobjectv
#undef map_mapstringv
}

melt_ptr_t
melt_get_mapstrings (struct meltmapstrings_st*mapstring_p,
                     const char *attr)
{
  long ix = 0, len = 0;
  const char *oldat = NULL;
  if (!mapstring_p || !attr)
    return NULL;
  if (mapstring_p->discr->meltobj_magic != MELTOBMAG_MAPSTRINGS)
    return NULL;
  if (!mapstring_p->entab)
    return NULL;
  len = melt_primtab[mapstring_p->lenix];
  if (len <= 0)
    return NULL;
  ix = unsafe_index_mapstring (mapstring_p->entab, attr, len);
  if (ix < 0 || !(oldat = mapstring_p->entab[ix].e_at)
      || oldat == HTAB_DELETED_ENTRY)
    return NULL;
  return mapstring_p->entab[ix].e_va;
}

melt_ptr_t
meltgc_remove_mapstrings (struct meltmapstrings_st *mapstring_p,
                          const char *attr)
{
  long ix = 0, len = 0, cnt = 0, atlen = 0;
  const char *oldat = NULL;
  char *attrdup = 0;
  char tinybuf[130];
  MELT_ENTERFRAME (3, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define mapstringv meltfram__.mcfr_varptr[1]
#define valuv meltfram__.mcfr_varptr[2]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define map_mapstringv ((struct meltmapstrings_st*)(mapstringv))
  mapstringv = (melt_ptr_t) mapstring_p;
  valuv = NULL;
  if (!mapstringv || !attr || !valuv || !attr[0])
    goto end;
  atlen = strlen (attr);
  discrv = (melt_ptr_t) map_mapstringv->discr;
  if (!discrv || object_discrv->meltobj_magic != MELTOBMAG_MAPSTRINGS)
    goto end;
  if (!map_mapstringv->entab)
    goto end;
  len = melt_primtab[map_mapstringv->lenix];
  if (len <= 0)
    goto end;
  if (atlen < (int) sizeof (tinybuf) - 1)
    {
      memset (tinybuf, 0, sizeof (tinybuf));
      attrdup = strcpy (tinybuf, attr);
    }
  else
    attrdup = strcpy ((char *) xcalloc (atlen + 1, 1), attr);
  ix = unsafe_index_mapstring (map_mapstringv->entab, attrdup, len);
  if (ix < 0 || !(oldat = map_mapstringv->entab[ix].e_at)
      || oldat == HTAB_DELETED_ENTRY)
    goto end;
  if (!melt_is_young (oldat))
    ggc_free (CONST_CAST (char *, oldat));
  map_mapstringv->entab[ix].e_at = (char *) HTAB_DELETED_ENTRY;
  valuv = map_mapstringv->entab[ix].e_va;
  map_mapstringv->entab[ix].e_va = NULL;
  map_mapstringv->count--;
  cnt = map_mapstringv->count;
  if (len > 7 && 2 * cnt + 2 < len)
    {
      int newcnt = 0, newlen = 0, newlenix = 0;
      size_t newlensiz = 0;
      struct entrystringsmelt_st *oldtab = NULL, *newtab = NULL;
      for (newlenix = map_mapstringv->lenix;
           (newlen = melt_primtab[newlenix]) > 2 * cnt + 3; newlenix--);
      if (newlen >= len)
        goto end;
      newlensiz = newlen * sizeof (struct entrystringsmelt_st);
      if (melt_is_young (mapstringv))
        {
          meltgc_reserve (newlensiz + 10 * sizeof (void *));
          if (!melt_is_young (mapstringv))
            goto alloc_old_mapstring_newtab;
          newtab =
            (struct entrystringsmelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
alloc_old_mapstring_newtab:
          newtab = ggc_alloc_cleared_vec_entrystringsmelt_st  (newlen);
        }
      oldtab = map_mapstringv->entab;
      for (ix = 0; ix < len; ix++)
        {
          const char *curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mapstring (newtab, curat, newlen);
          gcc_assert (newix >= 0);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in ol<d ggc space */
        ggc_free (oldtab);
      map_mapstringv->entab = newtab;
      map_mapstringv->count = newcnt;
    }
  meltgc_touch (map_mapstringv);
end:
  if (attrdup && attrdup != tinybuf)
    free (attrdup);
  MELT_EXITFRAME ();
  return (melt_ptr_t) valuv;
#undef discrv
#undef mapstringv
#undef valuv
#undef object_discrv
#undef map_mapstringv
}







/* index of entry to get or add an attribute in an mappointer (or -1 on error) */
struct GTY(()) entrypointermelt_st
{
  const void * e_at;
  melt_ptr_t e_va;
};
static inline int
unsafe_index_mappointer (struct entrypointermelt_st *tab,
                         const void *attr, int siz)
{
  int ix = 0, frix = -1;
  unsigned h = 0;
  if (!tab || !attr || siz <= 0)
    return -1;
  h = ((unsigned) (((long) (attr)) >> 3)) & MELT_MAXHASH;
  h = h % siz;
  for (ix = h; ix < siz; ix++)
    {
      const void *curat = tab[ix].e_at;
      if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
      else if (curat == attr)
        return ix;
    }
  for (ix = 0; ix < (int) h; ix++)
    {
      const void *curat = tab[ix].e_at;
      if (curat == (void *) HTAB_DELETED_ENTRY)
        {
          if (frix < 0)
            frix = ix;
        }
      else if (!curat)
        {
          if (frix < 0)
            frix = ix;
          return frix;
        }
      else if (curat == attr)
        return ix;
    }
  if (frix >= 0)    /* found some place in a table with deleted entries */
    return frix;
  return -1;      /* entirely full, should not happen */
}


/* this should be the same as meltmaptrees_st, meltmapedges_st,
   meltmapbasicblocks_st, .... */
struct meltmappointers_st
{
  meltobject_ptr_t discr;
  unsigned count;
  unsigned char lenix;
  unsigned meltmap_hash;
  melt_ptr_t meltmap_aux;
  struct entrypointermelt_st *entab;
  /* the following field is usually the value of entab (for
     objects in the young zone), to allocate the object and its fields
     at once */
  struct entrypointermelt_st map_space[MELT_FLEXIBLE_DIM];
};

#ifndef ggc_alloc_cleared_vec_entrypointermelt_st
/* When ggc_alloc_cleared_vec_entrypointermelt_st is not defined by
   gengtype generated files, we use the allocation of string entries
   suitably casted. This does not impact the GGC marking of struct
   meltmappointers_st since they are always casted & handled
   appropriately.  */
#define ggc_alloc_cleared_vec_entrypointermelt_st(Siz) \
  ((struct entrypointermelt_st*)(ggc_alloc_cleared_vec_entrystringsmelt_st(Siz)))
#endif /*ggc_alloc_cleared_vec_entrystringsmelt_st*/


/* allocate a new empty mappointers without checks */
void *
meltgc_raw_new_mappointers (meltobject_ptr_t discr_p, unsigned len)
{
  int lenix = 0, primlen = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define newmapv meltfram__.mcfr_varptr[1]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define map_newmapv ((struct meltmappointers_st*)(newmapv))
  discrv = (melt_ptr_t) discr_p;
  if (len > 0)
    {
      gcc_assert (len < (unsigned) MELT_MAXLEN);
      for (lenix = 1;
           (primlen = (int) melt_primtab[lenix]) != 0
           && primlen <= (int) len; lenix++);
    };
  gcc_assert (sizeof (struct entrypointermelt_st) ==
              sizeof (struct entrytreemelt_st));
  gcc_assert (sizeof (struct entrypointermelt_st) ==
              sizeof (struct entrygimplemelt_st));
  gcc_assert (sizeof (struct entrypointermelt_st) ==
              sizeof (struct entryedgemelt_st));
  gcc_assert (sizeof (struct entrypointermelt_st) ==
              sizeof (struct entrybasicblockmelt_st));
  meltgc_reserve (sizeof (struct meltmappointers_st)
                  + primlen * sizeof (struct entrypointermelt_st)
                  + 8 * sizeof(void*));
  newmapv = (melt_ptr_t)
            meltgc_allocate (offsetof
                             (struct meltmappointers_st,
                              map_space),
                             primlen * sizeof (struct entrypointermelt_st));
  map_newmapv->discr = object_discrv;
  map_newmapv->meltmap_aux = NULL;
  map_newmapv->meltmap_hash = melt_nonzerohash();
  map_newmapv->count = 0;
  map_newmapv->lenix = lenix;
  if (len > 0)
    map_newmapv->entab = map_newmapv->map_space;
  else
    map_newmapv->entab = NULL;
  MELT_EXITFRAME ();
  return newmapv;
#undef discrv
#undef newmapv
#undef object_discrv
#undef map_newmapv
}


void
meltgc_raw_put_mappointers (void *mappointer_p,
                            const void *attr, melt_ptr_t valu_p)
{
  long ix = 0, len = 0, cnt = 0;
  size_t lensiz = 0;
  MELT_ENTERFRAME (2, NULL);
#define mappointerv meltfram__.mcfr_varptr[0]
#define valuv meltfram__.mcfr_varptr[1]
#define map_mappointerv ((struct meltmappointers_st*)(mappointerv))
  mappointerv = (melt_ptr_t) mappointer_p;
  valuv = (melt_ptr_t) valu_p;
  if (!map_mappointerv->entab)
    {
      len = melt_primtab[1];  /* i.e. 3 */
      lensiz = len * sizeof (struct entrypointermelt_st);
      if (melt_is_young (mappointerv))
        {
          meltgc_reserve (lensiz + 10 * sizeof (void *));
          if (!melt_is_young (mappointerv))
            goto alloc_old_mappointer_small_entab;
          map_mappointerv->entab =
            (struct entrypointermelt_st *)
            melt_allocatereserved (lensiz, 0);
        }
      else
        {
alloc_old_mappointer_small_entab:
          map_mappointerv->entab
            = ggc_alloc_cleared_vec_entrypointermelt_st (len);
        }
      map_mappointerv->lenix = 1;
      meltgc_touch (map_mappointerv);
    }
  else if ((len = melt_primtab[map_mappointerv->lenix]) <=
           2 + (5 * (cnt = map_mappointerv->count)) / 4
           || (len <= 5 && cnt + 1 >= len))
    {
      int ix, newcnt = 0;
      int newlen = melt_primtab[map_mappointerv->lenix + 1];
      struct entrypointermelt_st *oldtab = NULL;
      struct entrypointermelt_st *newtab = NULL;
      size_t newlensiz = newlen * sizeof (struct entrypointermelt_st);
      if (melt_is_young (mappointerv))
        {
          meltgc_reserve (newlensiz + 10 * sizeof (void *));
          if (!melt_is_young (mappointerv))
            goto alloc_old_mappointer_entab;
          newtab =
            (struct entrypointermelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
alloc_old_mappointer_entab:
          newtab = ggc_alloc_cleared_vec_entrypointermelt_st (newlen);
        }
      oldtab = map_mappointerv->entab;
      for (ix = 0; ix < len; ix++)
        {
          const void *curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mappointer (newtab, curat, newlen);
          gcc_assert (newix >= 0 && newix < newlen);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in old ggc space */
        ggc_free (oldtab);
      map_mappointerv->entab = newtab;
      map_mappointerv->count = newcnt;
      map_mappointerv->lenix++;
      meltgc_touch (map_mappointerv);
      len = newlen;
    }
  ix = unsafe_index_mappointer (map_mappointerv->entab, attr, len);
  gcc_assert (ix >= 0 && ix < len);
  if (!map_mappointerv->entab[ix].e_at
      || map_mappointerv->entab[ix].e_at == HTAB_DELETED_ENTRY)
    {
      map_mappointerv->entab[ix].e_at = attr;
      map_mappointerv->count++;
    }
  map_mappointerv->entab[ix].e_va = (melt_ptr_t) valuv;
  meltgc_touch_dest (map_mappointerv, valuv);
  MELT_EXITFRAME ();
#undef discrv
#undef mappointerv
#undef valuv
#undef object_discrv
#undef map_mappointerv
}

melt_ptr_t
melt_raw_get_mappointers (void *map, const void *attr)
{
  long ix = 0, len = 0;
  const void *oldat = NULL;
  struct meltmappointers_st *mappointer_p =
  (struct meltmappointers_st *) map;
  if (!mappointer_p->entab)
    return NULL;
  len = melt_primtab[mappointer_p->lenix];
  if (len <= 0)
    return NULL;
  ix = unsafe_index_mappointer (mappointer_p->entab, attr, len);
  if (ix < 0 || !(oldat = mappointer_p->entab[ix].e_at)
      || oldat == HTAB_DELETED_ENTRY)
    return NULL;
  return mappointer_p->entab[ix].e_va;
}

melt_ptr_t
meltgc_raw_remove_mappointers (void *mappointer_p, const void *attr)
{
  long ix = 0, len = 0, cnt = 0;
  const char *oldat = NULL;
  MELT_ENTERFRAME (2, NULL);
#define mappointerv meltfram__.mcfr_varptr[0]
#define valuv meltfram__.mcfr_varptr[1]
#define map_mappointerv ((struct meltmappointers_st*)(mappointerv))
  mappointerv = (melt_ptr_t) mappointer_p;
  valuv = NULL;
  if (!map_mappointerv->entab)
    goto end;
  len = melt_primtab[map_mappointerv->lenix];
  if (len <= 0)
    goto end;
  ix = unsafe_index_mappointer (map_mappointerv->entab, attr, len);
  if (ix < 0 || !(oldat = (const char *) map_mappointerv->entab[ix].e_at)
      || oldat == HTAB_DELETED_ENTRY)
    goto end;
  map_mappointerv->entab[ix].e_at = (void *) HTAB_DELETED_ENTRY;
  valuv = map_mappointerv->entab[ix].e_va;
  map_mappointerv->entab[ix].e_va = NULL;
  map_mappointerv->count--;
  cnt = map_mappointerv->count;
  if (len > 7 && 2 * cnt + 2 < len)
    {
      int newcnt = 0, newlen = 0, newlenix = 0;
      struct entrypointermelt_st *oldtab = NULL, *newtab = NULL;
      size_t newlensiz = 0;
      for (newlenix = map_mappointerv->lenix;
           (newlen = melt_primtab[newlenix]) > 2 * cnt + 3; newlenix--);
      if (newlen >= len)
        goto end;
      newlensiz = newlen * sizeof (struct entrypointermelt_st);
      if (melt_is_young (mappointerv))
        {
          meltgc_reserve (newlensiz + 10 * sizeof (void *));
          if (!melt_is_young (mappointerv))
            goto allocate_old_newtab_mappointer;
          newtab =
            (struct entrypointermelt_st *)
            melt_allocatereserved (newlensiz, 0);
        }
      else
        {
allocate_old_newtab_mappointer:
          newtab =  ggc_alloc_cleared_vec_entrypointermelt_st (newlen);
        };
      oldtab = map_mappointerv->entab;
      for (ix = 0; ix < len; ix++)
        {
          const void *curat = oldtab[ix].e_at;
          int newix;
          if (!curat || curat == (void *) HTAB_DELETED_ENTRY)
            continue;
          newix = unsafe_index_mappointer (newtab, curat, newlen);
          gcc_assert (newix >= 0);
          newtab[newix] = oldtab[ix];
          newcnt++;
        }
      if (!melt_is_young (oldtab))
        /* free oldtab since it is in old ggc space */
        ggc_free (oldtab);
      map_mappointerv->entab = newtab;
      map_mappointerv->count = newcnt;
    }
  meltgc_touch (map_mappointerv);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) valuv;
#undef mappointerv
#undef valuv
#undef map_mappointerv
}


/***************** objvlisp test of strict subclassing */
bool
melt_is_subclass_of (meltobject_ptr_t subclass_p,
                     meltobject_ptr_t superclass_p)
{
  struct meltmultiple_st *subanc = NULL;
  struct meltmultiple_st *superanc = NULL;
  unsigned subdepth = 0, superdepth = 0;
  if (melt_magic_discr ((melt_ptr_t) subclass_p) !=
      MELTOBMAG_OBJECT || subclass_p->meltobj_magic != MELTOBMAG_OBJECT
      || melt_magic_discr ((melt_ptr_t) superclass_p) !=
      MELTOBMAG_OBJECT || superclass_p->meltobj_magic != MELTOBMAG_OBJECT)
    {
      return FALSE;
    }
  if (subclass_p->obj_len < MELTLENGTH_CLASS_CLASS
      || !subclass_p->obj_vartab
      || superclass_p->obj_len < MELTLENGTH_CLASS_CLASS || !superclass_p->obj_vartab)
    {
      return FALSE;
    }
  if (superclass_p == (meltobject_ptr_t) MELT_PREDEF (CLASS_ROOT))
    return TRUE;
  subanc =
    (struct meltmultiple_st *) subclass_p->obj_vartab[MELTFIELD_CLASS_ANCESTORS];
  superanc =
    (struct meltmultiple_st *) superclass_p->obj_vartab[MELTFIELD_CLASS_ANCESTORS];
  if (melt_magic_discr ((melt_ptr_t) subanc) != MELTOBMAG_MULTIPLE
      || subanc->discr != (meltobject_ptr_t) MELT_PREDEF (DISCR_CLASS_SEQUENCE))
    {
      return FALSE;
    }
  if (melt_magic_discr ((melt_ptr_t) superanc) != MELTOBMAG_MULTIPLE
      || superanc->discr != (meltobject_ptr_t) MELT_PREDEF (DISCR_CLASS_SEQUENCE))
    {
      return FALSE;
    }
  subdepth = subanc->nbval;
  superdepth = superanc->nbval;
  if (subdepth <= superdepth)
    return FALSE;
  if ((melt_ptr_t) subanc->tabval[superdepth] ==
      (melt_ptr_t) superclass_p)
    return TRUE;
  return FALSE;
}


melt_ptr_t
meltgc_new_string_raw_len (meltobject_ptr_t discr_p, const char *str, int slen)
{
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  strv = 0;
  if (!str)
    goto end;
  if (slen<0)
    slen = strlen (str);
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  strv = (melt_ptr_t) meltgc_allocate (sizeof (struct meltstring_st), slen + 1);
  str_strv->discr = obj_discrv;
  memcpy (str_strv->val, str, slen);
  str_strv->val[slen] = (char)0;
  str_strv->slen = slen;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}

melt_ptr_t
meltgc_new_string (meltobject_ptr_t discr_p, const char *str)
{
  return meltgc_new_string_raw_len(discr_p, str, -1);
}

melt_ptr_t
meltgc_new_stringdup (meltobject_ptr_t discr_p, const char *str)
{
  int slen = 0;
  char tinybuf[80];
  char *strcop = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  strv = 0;
  if (!str)
    goto end;
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  slen = strlen (str);
  if (slen < (int) sizeof (tinybuf) - 1)
    {
      memset (tinybuf, 0, sizeof (tinybuf));
      strcop = strcpy (tinybuf, str);
    }
  else
    strcop = strcpy ((char *) xcalloc (1, slen + 1), str);
  strv = meltgc_new_string_raw_len (obj_discrv, strcop, slen);
end:
  if (strcop && strcop != tinybuf)
    free (strcop);
  memset (tinybuf, 0, sizeof (tinybuf));
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}


/* Return a new string of given discriminant, with the original STR
   amputed of a given SUFFIX if appropriate, or else a copy of STR.  */
melt_ptr_t
meltgc_new_string_without_suffix (meltobject_ptr_t discr_p,
                                  const char* str,
                                  const char* suffix)
{
  char tinybuf[120];
  char *buf = NULL;
  int slen = 0;
  int suflen = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  memset (tinybuf, 0, sizeof(tinybuf));
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  if (!str)
    goto end;
  melt_debugeprintf ("meltgc_new_string_without_suffix str '%s' suffix '%s'",
                     str, suffix);
  slen = strlen (str);
  if (slen < (int) sizeof(tinybuf) - 1)
    {
      strcpy (tinybuf, str);
      buf = tinybuf;
    }
  else
    buf = xstrdup (str);
  if (!suffix)
    {
      suflen = 0;
      suffix = "";
    }
  else
    suflen = strlen (suffix);
  if (suflen <= slen && !strcmp (buf + slen - suflen, suffix))
    {
      buf[slen-suflen] = (char)0;
      strv = meltgc_new_string_raw_len (obj_discrv, buf, slen - suflen);
    }
  else
    {
      strv = meltgc_new_string_raw_len (obj_discrv, buf, slen);
    }
end:
  if (buf && buf != tinybuf)
    free (buf), buf = NULL;
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}


melt_ptr_t
meltgc_new_string_generated_cc_filename  (meltobject_ptr_t discr_p,
    const char* basepath,
    const char* dirpath,
    int num)
{
  int slen = 0;
  int spos = 0;
  char *strcop = NULL;
  char numbuf[16];
  char tinybuf[120];
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  memset (numbuf, 0, sizeof(numbuf));
  memset (tinybuf, 0, sizeof(tinybuf));
  discrv = (melt_ptr_t) discr_p;
  if (!basepath || !basepath[0])
    goto end;
  if (num > 0)
    snprintf (numbuf, sizeof(numbuf)-1, "+%02d", num);
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  slen += strlen (basepath);
  if (dirpath)
    slen += strlen (dirpath);
  slen += strlen (numbuf);
  slen += 6;
  /* slen is now an over-approximation of the needed space */
  if (slen < (int) sizeof(tinybuf)-1)
    strcop = tinybuf;
  else
    strcop = (char*) xcalloc (slen+1, 1);
  if (dirpath)
    {
      /* add the dirpath with a trailing slash if needed */
      strcpy (strcop, dirpath);
      spos = strlen (strcop);
      if (spos>0 && strcop[spos-1] != '/')
        strcop[spos++] = '/';
      /* add the basename of the basepath */
      strcpy (strcop + spos, melt_basename (basepath));
    }
  else
    {
      /* no dirpath, add the entire basepath */
      strcpy (strcop, basepath);
    };
  spos = strlen (strcop);
  /* if strcop ends with .c, remove that suffix */
  if (spos>2 && strcop[spos-1] == 'c' && strcop[spos-2] == '.')
    {
      strcop[spos-2] = strcop[spos-1] = (char)0;
      spos -= 2;
    }
  /* if strcop ends with .cc, remove that suffix */
  if (spos>3 && strcop[spos-1] == 'c' && strcop[spos-2] == 'c' && strcop[spos-3] == '.')
    {
      strcop[spos-3] = strcop[spos-2] = strcop[spos-1] = (char)0;
      spos -= 3;
    }
  /* remove the MELT_DYNLOADED_SUFFIX suffix [often .so] if given */
  else if (spos >= (int) sizeof(MELT_DYNLOADED_SUFFIX)
           && !strcmp (strcop+spos-(sizeof(MELT_DYNLOADED_SUFFIX)-1),
                       MELT_DYNLOADED_SUFFIX))
    {
      memset (strcop + spos - (sizeof(MELT_DYNLOADED_SUFFIX)-1),
              0, sizeof(MELT_DYNLOADED_SUFFIX)-1);
      spos -= sizeof(MELT_DYNLOADED_SUFFIX)-1;
    }
  /* remove the .melt suffix if given */
  else if (spos>5 && !strcmp (strcop+spos-5, ".melt"))
    {
      memset(strcop+spos, 0, strlen(".melt"));
      spos -= strlen(".melt");
    }
  strcpy (strcop + spos, numbuf);
  strcat (strcop + spos, ".cc");
  spos = strlen (strcop);
  gcc_assert (spos < slen-1);
  strv = meltgc_new_string_raw_len (obj_discrv, strcop, spos);
end:
  if (strcop && strcop != tinybuf)
    free (strcop);
  memset (tinybuf, 0, sizeof (tinybuf));
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}


melt_ptr_t
meltgc_new_string_nakedbasename (meltobject_ptr_t discr_p,
                                 const char *str)
{
  int slen = 0;
  char tinybuf[120];
  char *strcop = 0;
  const char *basestr = 0;
  char *dot = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  strv = 0;
  discrv = (melt_ptr_t) discr_p;
  melt_debugeprintf ("meltgc_new_string_nakedbasename start str '%s'", str);
  if (!str)
    goto end;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  slen = strlen (str);
  if (slen < (int) sizeof (tinybuf) - 1)
    {
      memset (tinybuf, 0, sizeof (tinybuf));
      strcop = strcpy (tinybuf, str);
    }
  else
    strcop = strcpy ((char *) xcalloc (1, slen + 1), str);
  basestr = (const char *) melt_basename (strcop);
  dot = CONST_CAST (char*, strrchr (basestr, '.'));
  if (dot)
    *dot = 0;
  slen = strlen (basestr);
  strv = meltgc_new_string_raw_len (obj_discrv, basestr, slen);
end:
  if (strcop && strcop != tinybuf)
    free (strcop);
  memset (tinybuf, 0, sizeof (tinybuf));
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}


melt_ptr_t
meltgc_new_string_tempname_suffixed (meltobject_ptr_t
                                     discr_p, const char *namstr, const char *suffstr)
{
  int slen = 0;
  char suffix[16];
  const char *basestr = xstrdup (melt_basename (namstr));
  const char* tempnampath = 0;
  char *dot = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  memset(suffix, 0, sizeof(suffix));
  if (suffstr) strncpy(suffix, suffstr, sizeof(suffix)-1);
  if (basestr)
    dot = CONST_CAST (char*, strrchr(basestr, '.'));
  if (dot)
    *dot=0;
  tempnampath = melt_tempdir_path (basestr, suffix);
  dbgprintf ("new_string_tempbasename basestr='%s' tempnampath='%s'", basestr, tempnampath);
  free(CONST_CAST(char*,basestr));
  basestr = 0;
  strv = 0;
  if (!tempnampath)
    goto end;
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  slen = strlen (tempnampath);
  strv = meltgc_new_string_raw_len (obj_discrv, tempnampath, slen);
end:
  if (tempnampath)
    free (CONST_CAST (char*,tempnampath));
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
#undef obj_discrv
#undef str_strv
}



/* Return as a cached MELT string, using the
   :sysdata_src_loc_file_dict dictonnary for memoization, the file
   path of a location, or else NULL. */
melt_ptr_t
meltgc_cached_string_path_of_source_location (source_location loc)
{
  const char* filepath = NULL;
  MELT_ENTERFRAME (2, NULL);
#define dictv     meltfram__.mcfr_varptr[0]
#define strv      meltfram__.mcfr_varptr[1]
  strv = NULL;
  if (loc == UNKNOWN_LOCATION)
    goto end;
  filepath = LOCATION_FILE (loc);
  if (!filepath)
    goto end;
  dictv = melt_get_inisysdata (MELTFIELD_SYSDATA_SRC_LOC_FILE_DICT);
  if (melt_magic_discr ((melt_ptr_t) dictv) == MELTOBMAG_MAPSTRINGS)
    {
      strv = melt_get_mapstrings ((struct meltmapstrings_st *) dictv,
                                  filepath);
      if (!strv)
        {
          strv = meltgc_new_stringdup ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),
                                       filepath);
          meltgc_put_mapstrings ((struct meltmapstrings_st*) dictv,
                                 filepath,
                                 (melt_ptr_t) strv);
        }
    }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef dictv
#undef strv
}


/* Split a string into a list of string value using sep as separating character.
*/
melt_ptr_t
meltgc_new_split_string (const char*str, int sep, melt_ptr_t discr_p)
{
  char* dupstr = 0;
  char *cursep = 0;
  char *pc = 0;
  MELT_ENTERFRAME (4, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define lisv       meltfram__.mcfr_varptr[2]
#define obj_discrv  ((struct meltobject_st*)(discrv))
#define str_strv  ((struct meltstring_st*)(strv))
  discrv = discr_p;
  if (!str)
    goto end;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (obj_discrv->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  dupstr = xstrdup (str);
  if (sep<0)
    sep=',';
  else if (sep==0)
    sep=' ';
  if (sep<0 || sep>CHAR_MAX)
    goto end;
  lisv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
  for (pc = dupstr; pc && *pc; pc = cursep?(cursep+1):0)
    {
      cursep = NULL;
      strv = NULL;
      /* avoid errors when we have str which starts with the separator or when
         we have a separator immediatly followed by another one (like
         'first::second').
      */
      while (*pc == sep)
        pc++;
      if (ISSPACE (sep))
        for (cursep=pc; *cursep && !ISSPACE (*cursep); cursep++);
      else
        for (cursep=pc; *cursep && *cursep != sep; cursep++);
      if (cursep && cursep>pc)
        strv = meltgc_new_string_raw_len (obj_discrv, pc, cursep-pc);
      else
        strv = meltgc_new_string_raw_len (obj_discrv, pc, strlen (pc));
      meltgc_append_list ((melt_ptr_t) lisv, (melt_ptr_t) strv);
      if (cursep && *cursep == 0)
        break;
    }
end:
  MELT_EXITFRAME ();
  free (dupstr);
  return (melt_ptr_t)lisv;
#undef discrv
#undef strv
#undef lisv
#undef str_strv
#undef obj_discrv
}


/* Return a string of a given discriminant (default DISCR_STRING), for
   the real path of a filepath which is an accessible file [perhaps a
   directory, etc...], or else NULL */
melt_ptr_t
meltgc_new_real_accessible_path_string (meltobject_ptr_t discr_p, const char *str)
{
  char *rpstr = NULL;
  MELT_ENTERFRAME (2, NULL);
#define discrv     meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
  discrv = (melt_ptr_t) discr_p;
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_STRING);
  if (melt_magic_discr ((melt_ptr_t) discrv) != MELTOBMAG_OBJECT)
    goto end;
  if (((meltobject_ptr_t) discrv)->meltobj_magic != MELTOBMAG_STRING)
    goto end;
  if (!str || !str[0] || access (str, F_OK))
    goto end;
  rpstr = lrealpath (str);
  if (!rpstr || !rpstr[0])
    goto end;
  strv = meltgc_new_string_raw_len ((meltobject_ptr_t)discrv, rpstr, strlen (rpstr));
end:
  free (rpstr);
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef discrv
#undef strv
}




static long applcount_melt;
static int appldepth_melt;
#define MAXDEPTH_APPLY_MELT 512
long melt_application_count (void)
{
  if (melt_flag_debug > 0)
    return (long) applcount_melt;
  else return 0;
}
long melt_application_depth (void)
{
  if (melt_flag_debug > 0)
    return (long) appldepth_melt;
  else return 0;
}

/*************** closure application ********************/

/* the argument description string are currently char* strings; this
   could be changed to wchar_t* strings when the number of generated
   ctypes and MELBPAR__LAST is becoming large.  Also update the
   generate_runtypesupport_param function of warmelt-outobj.melt.  So
   the test at end of generate_runtypesupport_param should be kept in
   sync with the maximal value. See comments around
   melt_argdescr_cell_t and MELT_ARGDESCR_MAX in melt-runtime.h and
   keep delicately in sync with warmelt-outobj.melt code. */
melt_ptr_t
melt_apply (meltclosure_ptr_t clos_p,
            melt_ptr_t arg1_p,
            const melt_argdescr_cell_t *xargdescr_,
            union meltparam_un *xargtab_,
            const melt_argdescr_cell_t *xresdescr_,
            union meltparam_un *xrestab_)
{
  melt_ptr_t res = NULL;
  meltroutfun_t*routfun = 0;
  if (MELT_HAVE_RUNTIME_DEBUG > 0 || melt_flag_debug > 0)
    {
      applcount_melt++;
      appldepth_melt++;
      if (appldepth_melt > MAXDEPTH_APPLY_MELT)
        {
          melt_fatal_error ("too deep (%d) MELT applications", appldepth_melt);
        }
      if ((int) MELTBPAR__LAST >= (int) MELT_ARGDESCR_MAX - 2)
        melt_fatal_error ("too many different MELT ctypes since MELTBPAR__LAST= %d",
                          (int) MELTBPAR__LAST);
    }
  if (melt_magic_discr ((melt_ptr_t) clos_p) != MELTOBMAG_CLOSURE)
    goto end;
  {
    int routmag = melt_magic_discr ((melt_ptr_t) (clos_p->rout));
    if (routmag != MELTOBMAG_ROUTINE)
      {
        melt_fatal_error ("MELT corrupted closure %p with routine value %p of bad magic %d (expecting MELTOBMAG_ROUTINE=%d)",
                          (void*) clos_p, (void*) clos_p->rout,
                          routmag, MELTOBMAG_ROUTINE);
        goto end;
      }
  }
  if (!(routfun = clos_p->rout->routfunad))
    {
      melt_fatal_error ("MELT closure %p with corrupted routine value %p <%s> without function",
                        (void*) clos_p, (void*) clos_p->rout,
                        clos_p->rout->routdescr);
      goto end;
    }
  res = (*routfun) (clos_p, arg1_p, xargdescr_, xargtab_, xresdescr_, xrestab_);
end:
  if (MELT_HAVE_RUNTIME_DEBUG>0 || melt_flag_debug>0)
    appldepth_melt--;
  return res;
}



/************** method sending ***************/
melt_ptr_t
meltgc_send (melt_ptr_t recv_p,
             melt_ptr_t sel_p,
             const melt_argdescr_cell_t *xargdescr_,
             union meltparam_un * xargtab_,
             const melt_argdescr_cell_t *xresdescr_,
             union meltparam_un * xrestab_)
{
  /** NAUGHTY TRICK here: message sending is very common, and we want
     to avoid having the current frame (the frame declared by the
     MELT_ENTERFRAME macro call below) to be active when the application
     for the sending is performed. This should make our call frames'
     linked list shorter. To do so, we put the closure to apply and
     the receiver in the two variables below. Yes this is dirty, but
     it works!

     We should be very careful when modifying this routine.  Never
     assign to these dirtyptr-s if a GC could happen!  */
  meltclosure_ptr_t closure_dirtyptr = NULL;
  melt_ptr_t recv_dirtyptr = NULL;

  MELT_ENTERFRAME (7, NULL);
#define recv    meltfram__.mcfr_varptr[0]
#define selv    meltfram__.mcfr_varptr[1]
#define closv   meltfram__.mcfr_varptr[2]
#define discrv  meltfram__.mcfr_varptr[3]
#define mapv    meltfram__.mcfr_varptr[4]
#define resv    meltfram__.mcfr_varptr[5]
#define ancv    meltfram__.mcfr_varptr[6]
#define obj_discrv ((meltobject_ptr_t)(discrv))
#define obj_selv ((meltobject_ptr_t)(selv))
#define clo_closv ((meltclosure_ptr_t)(closv))
#define mul_ancv  ((struct meltmultiple_st*)(ancv))
  recv = recv_p;
  selv = sel_p;
  MELT_LOCATION_HERE ("sending msg");
  /* the receiver can be null, using DISCR_NULL_RECEIVER */
  if (melt_magic_discr ((melt_ptr_t) selv) != MELTOBMAG_OBJECT)
    goto end;
  if (!melt_is_instance_of
      ((melt_ptr_t) selv, (melt_ptr_t) MELT_PREDEF (CLASS_SELECTOR)))
    goto end;
  if (recv != NULL)
    {
      discrv = (melt_ptr_t) ((melt_ptr_t) recv)->u_discr;
      gcc_assert (discrv != NULL);
    }
  else
    {
      discrv = (melt_ptr_t) ((meltobject_ptr_t) MELT_PREDEF (DISCR_NULL_RECEIVER));
      gcc_assert (discrv != NULL);
    };
#if MELT_HAVE_DEBUG
  char curloc[80];
  curloc[0] = (char)0;
  if (melt_flag_debug)
    {
      const char* selname = melt_string_str (obj_selv->obj_vartab[MELTFIELD_NAMED_NAME]);
      if (selname && selname[0])
        MELT_LOCATION_HERE_PRINTF (curloc, "sending %s", selname);
    }
#endif /*MELT_HAVE_DEBUG*/
  while (discrv)
    {
      gcc_assert (melt_magic_discr ((melt_ptr_t) discrv) ==
                  MELTOBMAG_OBJECT);
      gcc_assert (obj_discrv->obj_len >= MELTLENGTH_CLASS_DISCRIMINANT);
      mapv = obj_discrv->obj_vartab[MELTFIELD_DISC_METHODICT];
      if (melt_magic_discr ((melt_ptr_t) mapv) == MELTOBMAG_MAPOBJECTS)
        {
          closv =
            (melt_ptr_t) melt_get_mapobjects ((meltmapobjects_ptr_t)
                                              mapv,
                                              (meltobject_ptr_t)
                                              selv);
        }
      else
        {
          closv = obj_discrv->obj_vartab[MELTFIELD_DISC_SENDER];
          if (melt_magic_discr ((melt_ptr_t) closv) == MELTOBMAG_CLOSURE)
            {
              union meltparam_un pararg[1];
              pararg[0].meltbp_aptr = (melt_ptr_t *) & selv;
              resv =
                melt_apply ((meltclosure_ptr_t) closv, // sending a message
                            (melt_ptr_t) recv, MELTBPARSTR_PTR, pararg, "",
                            NULL);
              closv = resv;
            }
        }
      if (melt_magic_discr ((melt_ptr_t) closv) == MELTOBMAG_CLOSURE)
        {
          /* NAUGHTY TRICK: assign to dirty (see comments near start of function) */
          closure_dirtyptr = (meltclosure_ptr_t) closv;
          recv_dirtyptr = (melt_ptr_t) recv;
          /*** OLD CODE:
               resv =
               melt_apply (closv, recv, xargdescr_, xargtab_, // sending a message, old code
               xresdescr_, xrestab_);
          ***/
          goto end;
        }
      discrv = obj_discrv->obj_vartab[MELTFIELD_DISC_SUPER];
    }       /* end while discrv */
  resv = NULL;
end:
  MELT_EXITFRAME ();
  /* NAUGHTY TRICK  (see comments near start of function) */
  if (closure_dirtyptr)
    return melt_apply (closure_dirtyptr, recv_dirtyptr, xargdescr_, // dirty trick sending
                       xargtab_, xresdescr_, xrestab_);
  return (melt_ptr_t) resv;
#undef recv
#undef selv
#undef closv
#undef discrv
#undef mapv
#undef resv
#undef ancv
#undef obj_discrv
#undef obj_selv
#undef clo_closv
}



/* Clear a slot inside the INITIAL_SYSTEM_DATA. */
static inline void
melt_clear_inisysdata(int off)
{
  meltobject_ptr_t inisys = (meltobject_ptr_t) MELT_PREDEF(INITIAL_SYSTEM_DATA);
  if (melt_magic_discr ((melt_ptr_t) inisys) == MELTOBMAG_OBJECT)
    {
      int leninisys = inisys->obj_len;
      gcc_assert(melt_is_instance_of
                 ((melt_ptr_t) inisys,
                  (melt_ptr_t) MELT_PREDEF (CLASS_SYSTEM_DATA)));
      if (off>=0 && off<leninisys)
        {
          /* Don't bother to call meltgc_touch because we simply clear
             a pointer slot in the initial_system_data, so this cannot
             add naughty old to young references. */
          inisys->obj_vartab[off] = NULL;
        }
    }
}

/* our temporary directory */
/* maybe it should not be static, or have a bigger length */
static char melt_tempdir[1024];

static bool melt_made_tempdir;
/* returns malloc-ed path inside a temporary directory, with a given basename  */
char *
melt_tempdir_path (const char *srcnam, const char* suffix)
{
  int loopcnt = 0;
  int mkdirdone = 0;
  const char *basnam = 0;
  static const char* tmpdirstr = 0;
  time_t nowt = 0;
  int maymkdir = srcnam != NULL;
  basnam = srcnam?melt_basename (CONST_CAST (char*,srcnam)):0;
  melt_debugeprintf ("melt_tempdir_path srcnam '%s' basnam '%s' suffix '%s'", srcnam, basnam, suffix);
  if (!tmpdirstr)
    tmpdirstr = melt_argument ("tempdir");
  gcc_assert (!basnam || (ISALNUM (basnam[0]) || basnam[0] == '_'));
  if (tmpdirstr && tmpdirstr[0])
    {
      if (maymkdir && access (tmpdirstr, F_OK))
        {
          if (mkdir (tmpdirstr, 0700))
            melt_fatal_error ("failed to mkdir melt_tempdir %s - %m",
                              tmpdirstr);
          melt_made_tempdir = true;
        }
      return concat (tmpdirstr, "/", basnam, suffix, NULL);
    }
  if (!melt_tempdir[0])
    {
      if (!maymkdir)
        return NULL;
      time (&nowt);
      /* Usually this loop runs only once!  */
      for (loopcnt = 0; loopcnt < 1000; loopcnt++)
        {
          int n = (melt_lrand () & 0x1fffffff) ^ (nowt & 0xffffff);
          n += (int)getpid ();
          memset(melt_tempdir, 0, sizeof(melt_tempdir));
          snprintf (melt_tempdir, sizeof(melt_tempdir)-1,
                    "%s-GccMeltTmp-%x",
                    tmpnam(NULL),  n);
          if (!mkdir (melt_tempdir, 0700))
            {
              melt_made_tempdir = true;
              mkdirdone = 1;
              break;
            };
        }
      if (!mkdirdone)
        melt_fatal_error ("failed to create temporary directory for MELT, last try was %s - %m", melt_tempdir);
    };
  return concat (melt_tempdir, "/", basnam, suffix, NULL);
}



/* utility to add an escaped file path into an obstack. Returns true if characters have been escaped */
static bool
obstack_add_escaped_path (struct obstack* obs, const char* path)
{
  bool warn = false;
  const char* pc;
  for (pc = path; *pc; pc++)
    {
      const char c = *pc;
      /* Accept ordinary characters as is. */
      if (ISALNUM(c) || c == '/' || c == '.' || c == '_' || c == '-' || c == '+')
        {
          obstack_1grow (obs, c);
          continue;
        }
      /* Accept characters as is if they are not first or last.  */
      if (pc > path && pc[1]
          && (c == '=' || c == ':'))
        {
          obstack_1grow (obs, c);
          continue;
        }
      /* Escape other characters.
         FIXME:  this could be not enough with UTF8 special characters!  */
      warn = true;
      obstack_1grow (obs, '\\');
      obstack_1grow (obs, c);
    };
  return warn;
}



/* the name of the source module argument to 'make' without any .c suffix. */
#define MODULE_SOURCEBASE_ARG "GCCMELT_MODULE_SOURCEBASE="
/* the name of the binary base argument to 'make'. No dots in the
   basename here... */
#define MODULE_BINARYBASE_ARG "GCCMELT_MODULE_BINARYBASE="
/* the name of the workspace directory */
#define WORKSPACE_ARG "GCCMELT_MODULE_WORKSPACE="
/* flavor of the binary module */
#define FLAVOR_ARG "GCCMELT_MODULE_FLAVOR="

/* do we build with C++ the generated C modules */
#define BUILD_WITH_CXX_ARG "MELTGCC_BUILD_WITH_CXX="

/* the additional C flags */
#define CFLAGS_ARG "GCCMELT_CFLAGS="
/* the flag to change directory for make */
/* See also file melt-module.mk which expects the module binary to
   be without its MELT_DYNLOADED_SUFFIX. */
#define MAKECHDIR_ARG "-C"

/* the make target */
#define MAKE_TARGET "melt_module"



#if MELT_IS_PLUGIN
static void
melt_run_make_for_plugin (const char*ourmakecommand, const char*ourmakefile, const char*ourcflags,
                          const char*flavor, const char*srcbase, const char*binbase,
                          const char*workdir)
{
  /* In plugin mode, we sadly don't have the pex_run function
     available, because libiberty is statically linked into cc1
     which don't need pex_run.  See
     http://gcc.gnu.org/ml/gcc-patches/2009-11/msg01419.html etc.
     So we unfortunately have to use system(3), using an obstack for
     the command string. */
  int err = 0;
  bool warnescapedchar = false;
  char *cmdstr = NULL;
  const char*mycwd = getpwd ();
  struct obstack cmd_obstack;
  memset (&cmd_obstack, 0, sizeof(cmd_obstack));
  obstack_init (&cmd_obstack);
  melt_debugeprintf ("starting melt_run_make_for_plugin ourmakecommand=%s ourmakefile=%s ourcflags=%s",
                     ourmakecommand, ourmakefile, ourcflags);
  melt_debugeprintf ("starting melt_run_make_for_plugin flavor=%s srcbase=%s binbase=%s workdir=%s pwd=%s",
                     flavor, srcbase, binbase, workdir, mycwd);
  if (!flavor)
    flavor = MELT_DEFAULT_FLAVOR;

  /* add ourmakecommand without any quoting trickery! */
  obstack_grow (&cmd_obstack, ourmakecommand, strlen(ourmakecommand));
  obstack_1grow (&cmd_obstack, ' ');
  /* silent make if not debugging */
  if (!melt_flag_debug)
    obstack_grow (&cmd_obstack, "-s ", 3);
  /* add -f with spaces */
  obstack_grow (&cmd_obstack, "-f ", 3);
  /* add ourmakefile and escape with backslash every escaped chararacter */
  warnescapedchar = obstack_add_escaped_path (&cmd_obstack, ourmakefile);
  if (warnescapedchar)
    warning (0, "escaped character[s] in MELT module makefile %s", ourmakefile);
  obstack_1grow (&cmd_obstack, ' ');

  /* add the source argument */
  obstack_grow (&cmd_obstack, MODULE_SOURCEBASE_ARG, strlen (MODULE_SOURCEBASE_ARG));
  if (!IS_ABSOLUTE_PATH(srcbase))
    {
      (void) obstack_add_escaped_path (&cmd_obstack, mycwd);
      obstack_1grow (&cmd_obstack, '/');
    }
  warnescapedchar = obstack_add_escaped_path (&cmd_obstack, srcbase);
  if (warnescapedchar)
    warning (0, "escaped character[s] in MELT source base %s", srcbase);
  obstack_1grow (&cmd_obstack, ' ');

  /* add the binary argument */
  obstack_grow (&cmd_obstack, MODULE_BINARYBASE_ARG,
                strlen (MODULE_BINARYBASE_ARG));
  if (!IS_ABSOLUTE_PATH (binbase))
    {
      (void) obstack_add_escaped_path (&cmd_obstack, mycwd);
      obstack_1grow (&cmd_obstack, '/');
    }
  warnescapedchar = obstack_add_escaped_path (&cmd_obstack, binbase);
  if (warnescapedchar)
    warning (0, "escaped character[s] in MELT binary module %s", binbase);
  obstack_1grow (&cmd_obstack, ' ');

  /* add the built with C++ argument */
  {
    obstack_1grow (&cmd_obstack, ' ');
    obstack_grow (&cmd_obstack, BUILD_WITH_CXX_ARG "YesPlugin",
                  strlen (BUILD_WITH_CXX_ARG "YesPlugin"));
  }

  /* add the cflag argument if needed */
  if (ourcflags && ourcflags[0])
    {
      melt_debugeprintf ("melt_run_make_for_plugin ourcflags=%s", ourcflags);
      obstack_1grow (&cmd_obstack, ' ');
      /* don't warn about escapes for cflags, they contain spaces...*/
      melt_debugeprintf ("melt_run_make_for_plugin CFLAGS_ARG=%s", CFLAGS_ARG);
      obstack_grow (&cmd_obstack, CFLAGS_ARG, strlen (CFLAGS_ARG));
      obstack_add_escaped_path (&cmd_obstack, ourcflags);
      obstack_1grow (&cmd_obstack, ' ');
      obstack_1grow (&cmd_obstack, ' ');
    };

  /* add the workspace argument if needed, that is if workdir is
     provided not as '.' */
  if (workdir && workdir[0] && strcmp(workdir,".") && strcmp(workdir, mycwd))
    {
      struct stat workstat;
      memset (&workstat, 0, sizeof(workstat));
      melt_debugeprintf ("melt_run_make_for_plugin handling workdir %s", workdir);
      if (stat (workdir, &workstat))
        melt_fatal_error ("bad MELT module workspace directory %s - stat failed %m", workdir);
      if (!S_ISDIR(workstat.st_mode))
        melt_fatal_error ("MELT module workspace %s not directory",
                          workdir);
      obstack_grow (&cmd_obstack, WORKSPACE_ARG, strlen (WORKSPACE_ARG));
      if (!IS_ABSOLUTE_PATH(workdir))
        {
          (void) obstack_add_escaped_path (&cmd_obstack, mycwd);
          obstack_1grow (&cmd_obstack, '/');
        };
      warnescapedchar = obstack_add_escaped_path (&cmd_obstack, workdir);
      if (warnescapedchar)
        warning (0, "escaped character[s] in MELT workspace directory %s", workdir);
      obstack_1grow (&cmd_obstack, ' ');
    }

  /* Add the flavor and the constant make target*/
  obstack_grow (&cmd_obstack, FLAVOR_ARG, strlen (FLAVOR_ARG));
  warnescapedchar = obstack_add_escaped_path (&cmd_obstack, flavor);
  if (warnescapedchar)
    warning (0, "escaped character[s] in MELT flavor %s", flavor);
  obstack_1grow (&cmd_obstack, ' ');
  obstack_grow (&cmd_obstack, MAKE_TARGET, strlen (MAKE_TARGET));
  obstack_1grow (&cmd_obstack, (char) 0);
  cmdstr = XOBFINISH (&cmd_obstack, char *);
  melt_debugeprintf("melt_run_make_for_plugin cmdstr= %s", cmdstr);
  if (!quiet_flag || melt_flag_bootstrapping)
    printf ("MELT plugin running: %s\n", cmdstr);
  fflush (NULL);
  err = system (cmdstr);
  melt_debugeprintf("melt_run_make_for_plugin command got %d", err);
  if (err)
    melt_fatal_error ("MELT plugin module compilation failed (%d) in %s for command %s",
                      err, getpwd (), cmdstr);
  cmdstr = NULL;
  obstack_free (&cmd_obstack, NULL); /* free all the cmd_obstack */
  melt_debugeprintf ("melt_run_make_for_plugin meltplugin did built binbase %s flavor %s in workdir %s",
                     binbase, flavor, workdir);
  if (IS_ABSOLUTE_PATH (binbase))
    inform (UNKNOWN_LOCATION, "MELT plugin has built module %s flavor %s", binbase, flavor);
  else
    inform (UNKNOWN_LOCATION, "MELT plugin has built module %s flavor %s in %s",
            binbase, flavor, mycwd);
  return;
}

#else

static void
melt_run_make_for_branch (const char*ourmakecommand, const char*ourmakefile, const char*ourcflags,
                          const char*flavor, const char*srcbase, const char*binbase, const char*workdir)
{
  int argc = 0;
  int err = 0;
  int cstatus = 0;
  const char *argv[25] = { NULL };
  const char *errmsg = NULL;
  char* srcarg = NULL;
  char* binarg = NULL;
  char* cflagsarg = NULL;
  char* workarg = NULL;
  char* flavorarg = NULL;
  char* mycwd = NULL;
  struct pex_obj* pex = NULL;
  struct pex_time ptime;
  double mysystime = 0.0, myusrtime = 0.0;
  char cputimebuf[32];
  memset (&ptime, 0, sizeof (ptime));
  memset (cputimebuf, 0, sizeof (cputimebuf));
  memset (argv, 0, sizeof(argv));
  mycwd = getpwd ();
  /* compute the ourmakecommand */
  pex = pex_init (PEX_RECORD_TIMES, ourmakecommand, NULL);
  argv[argc++] = ourmakecommand;
  melt_debugeprintf("melt_run_make_for_branch arg ourmakecommand %s", ourmakecommand);
  /* silent make if not debugging */
  if (!melt_flag_debug && quiet_flag)
    argv[argc++] = "-s";

  /* the -f argument, and then the makefile */
  argv[argc++] = "-f";
  argv[argc++] = ourmakefile;
  melt_debugeprintf("melt_run_make_for_branch arg ourmakefile %s", ourmakefile);

  /* the source base argument */
  if (IS_ABSOLUTE_PATH(srcbase))
    srcarg = concat (MODULE_SOURCEBASE_ARG, srcbase, NULL);
  else
    srcarg = concat (MODULE_SOURCEBASE_ARG, mycwd, "/", srcbase, NULL);
  argv[argc++] = srcarg;
  melt_debugeprintf ("melt_run_make_for_branch arg srcarg %s", srcarg);

  /* add the built with C++ argument if needed */
#if defined(ENABLE_BUILD_WITH_CXX) || MELT_GCC_VERSION >= 4008 || defined(__cplusplus)
  {
    const char*cplusarg = BUILD_WITH_CXX_ARG "YesBranch";
    argv[argc++] = cplusarg;
    melt_debugeprintf ("melt_run_make_for_branch arg with C++: %s", cplusarg);
  }
#endif /* endif */

  /* the binary base argument */
  if (IS_ABSOLUTE_PATH(binbase))
    binarg = concat (MODULE_BINARYBASE_ARG, binbase, NULL);
  else
    binarg = concat (MODULE_BINARYBASE_ARG, mycwd, "/", binbase, NULL);
  argv[argc++] = binarg;
  melt_debugeprintf("melt_run_make_for_branch arg binarg %s", binarg);
  if (ourcflags && ourcflags[0])
    {
      cflagsarg = concat (CFLAGS_ARG, ourcflags, NULL);
      melt_debugeprintf("melt_run_make_for_branch arg cflagsarg %s", cflagsarg);
      argv[argc++] = cflagsarg;
    }
  /* add the workspace argument if needed, that is if workdir is
     provided not as '.' */
  if (workdir && workdir[0] && (workdir[0] != '.' || workdir[1]))
    {
      struct stat workstat;
      melt_debugeprintf ("melt_run_make_for_branch handling workdir %s", workdir);
      memset (&workstat, 0, sizeof(workstat));
      if (stat (workdir, &workstat) || (!S_ISDIR (workstat.st_mode) && (errno = ENOTDIR) != 0))
        melt_fatal_error ("invalid MELT module workspace directory %s - %m", workdir);
      workarg = concat (WORKSPACE_ARG, workdir, NULL);
      argv[argc++] = workarg;
      melt_debugeprintf ("melt_run_make_for_branch arg workarg %s", workarg);
    }
  if (flavor && flavor[0])
    {
      flavorarg = concat (FLAVOR_ARG, flavor, NULL);
      argv[argc++] = flavorarg;
      melt_debugeprintf ("melt_run_make_for_branch arg flavorarg %s", flavorarg);
    }

  /* at last the target */
  argv[argc++] = MAKE_TARGET;
  /* terminate by null */
  argv[argc] = NULL;
  gcc_assert ((int) argc < (int) (sizeof(argv)/sizeof(*argv)));

  if (melt_flag_debug)
    {
      int i;
      melt_debugeprintf("melt_run_make_for_branch before pex_run argc=%d", argc);
      for (i=0; i<argc; i++)
        melt_debugeprintf ("melt_run_make_for_branch pex_run argv[%d]=%s", i, argv[i]);
    }
  if (!quiet_flag || melt_flag_bootstrapping)
    {
      int i = 0;
      char* cmdbuf = 0;
      struct obstack cmd_obstack;
      memset (&cmd_obstack, 0, sizeof(cmd_obstack));
      obstack_init (&cmd_obstack);
      for (i=0; i<argc; i++)
        {
          if (i>0)
            obstack_1grow (&cmd_obstack, ' ');
          obstack_add_escaped_path (&cmd_obstack, argv[i]);
        }
      obstack_1grow(&cmd_obstack, (char)0);
      cmdbuf = XOBFINISH (&cmd_obstack, char *);
      printf ("MELT branch running: %s\n", cmdbuf);
    }
  melt_debugeprintf("melt_run_make_for_branch before pex_run ourmakecommand='%s'", ourmakecommand);
  fflush (NULL);
  errmsg =
    pex_run (pex, PEX_LAST | PEX_SEARCH, ourmakecommand,
             CONST_CAST (char**, argv),
             NULL, NULL, &err);
  if (errmsg)
    melt_fatal_error
    ("MELT run make failed %s with source argument %s & binary argument %s : %s",
     ourmakecommand, srcarg, binarg, errmsg);
  if (!pex_get_status (pex, 1, &cstatus))
    melt_fatal_error
    ("failed to get status of MELT run %s with source argument %s & binary argument %s- %m",
     ourmakecommand, srcarg, binarg);
  if (!pex_get_times (pex, 1, &ptime))
    melt_fatal_error
    ("failed to get time of  MELT run %s with source argument %s & binary argument %s - %m",
     ourmakecommand, srcarg, binarg);
  if (cstatus)
    {
      int i = 0;
      char* cmdbuf = 0;
      struct obstack cmd_obstack;
      memset (&cmd_obstack, 0, sizeof(cmd_obstack));
      obstack_init (&cmd_obstack);
      for (i=0; i<argc; i++)
        {
          if (i>0)
            obstack_1grow (&cmd_obstack, ' ');
          obstack_add_escaped_path (&cmd_obstack, argv[i]);
        }
      obstack_1grow(&cmd_obstack, (char)0);
      cmdbuf = XOBFINISH (&cmd_obstack, char *);
      error ("MELT failed command: %s",  cmdbuf);
      melt_fatal_error
      ("MELT branch failed (%s %d) to build module using %s -f %s, source %s, binary %s, flavor %s",
       WIFEXITED (cstatus)?"exit"
       : WIFSIGNALED(cstatus)? "got signal"
       : WIFSTOPPED(cstatus)?"stopped"
       : "crashed",
       WIFEXITED (cstatus) ? WEXITSTATUS(cstatus)
       : WIFSIGNALED(cstatus) ? WTERMSIG(cstatus)
       : cstatus, ourmakecommand, ourmakefile,
       srcarg, binarg, flavorarg);
    }
  pex_free (pex);
  myusrtime = (double) ptime.user_seconds
              + 1.0e-6*ptime.user_microseconds;
  mysystime = (double) ptime.system_seconds
              + 1.0e-6*ptime.system_microseconds;
  melt_debugeprintf("melt_run_make_for_branch melt did built binfile %s in %.3f usrtime + %.3f systime", binarg, myusrtime, mysystime);
  snprintf (cputimebuf, sizeof(cputimebuf)-1, "%.3f", myusrtime + mysystime);
  if (IS_ABSOLUTE_PATH(binbase))
    inform (UNKNOWN_LOCATION,
            "MELT has built module %s in %s sec.",
            binbase, cputimebuf);
  else
    inform (UNKNOWN_LOCATION,
            "MELT has built module %s inside %s in %s sec.",
            binbase, mycwd, cputimebuf);

  melt_debugeprintf ("melt_run_make_for_branch done srcarg %s binarg %s flavorarg %s workarg %s",
                     srcarg, binarg, flavorarg, workarg);
  free (srcarg);
  free (binarg);
  free (flavorarg);
  free (workarg);
}
#endif /*MELT_IS_PLUGIN*/





/* the srcbase is a generated primary .c file without its .c suffix,
   such as /some/path/foo which also means the MELT descriptor file
   /some/path/foo+meltdesc.c and the MELT timestamp file
   /some/path/foo+melttime.h and possibly secondary files like
   /some/path/foo+01.c /some/path/foo+02.c in addition of the primary
   file /some/path/foo.c ; the binbase should have no
   MELT_DYNLOADED_SUFFIX.  The module build is done thru the
   melt-module.mk file [with the 'make' utility]. */

void
melt_compile_source (const char *srcbase, const char *binbase, const char*workdir, const char*flavor)
{
  /* The generated dynamic library should have the following
     constant strings:
     const char melt_compiled_timestamp[];
     const char melt_md5[];

     The melt_compiled_timestamp should contain a human readable
     timestamp the melt_md5 should contain the hexadecimal md5 digest,
     followed by the source file name (i.e. the single line output by the
     command: md5sum $Csourcefile; where $Csourcefile is replaced by the
     source file path)

  */
  char* srcdescrpath = NULL;
  const char* ourmakecommand = NULL;
  const char* ourmakefile = NULL;
  const char* ourcflags = NULL;
  const char* mycwd = NULL;
#if MELT_HAVE_RUNTIME_DEBUG >0
  char curlocbuf[250];
  curlocbuf[0] = 0;
#endif
  /* we want a MELT frame for MELT_LOCATION here */
  MELT_ENTEREMPTYFRAME(NULL);
  mycwd = getpwd ();
  if (!flavor)
    flavor = MELT_DEFAULT_FLAVOR;
  melt_debugeprintf ("melt_compile_source start srcbase %s binbase %s flavor %s",
                     srcbase, binbase, flavor);
  melt_debugeprintf ("melt_compile_source start workdir %s", workdir);
  melt_debugeprintf ("melt_compile_source start mycwd %s", mycwd);
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "melt_compile_source srcbase %s binbase %s flavor %s",
                             srcbase?(srcbase[0]?srcbase:"*empty*"):"*null*",
                             binbase?(binbase[0]?binbase:"*empty*"):"*null*",
                             flavor?(flavor[0]?flavor:"*empty*"):"*null*");
  if (getenv ("IFS"))
    /* Having an IFS is a huge security risk for shells. */
    melt_fatal_error
    ("MELT cannot compile source base %s of flavor %s with an $IFS (probable security risk)",
     srcbase, flavor);
  if (!srcbase || !srcbase[0])
    {
      melt_fatal_error ("no source base given to compile for MELT (%p)",
                        srcbase);
    }
  if (!binbase || !binbase[0])
    {
      melt_fatal_error ("no binary base given to compile %s for MELT", srcbase);
    }
  if (!workdir || !workdir[0])
    {
      workdir = melt_argument("workdir");
      if (!workdir)
        workdir = melt_tempdir_path (NULL, NULL);
    }
  srcdescrpath = concat (srcbase, MELT_DESC_FILESUFFIX, NULL);
  if (access (srcdescrpath, R_OK))
    melt_fatal_error ("Cannot access MELT descriptive file %s to compile - %m",
                      srcdescrpath);
  {
    char*timefpath = concat (srcbase, MELT_TIME_FILESUFFIX, NULL);
    if (access (timefpath, R_OK))
      warning(0, "MELT timestamp file %s missing", timefpath);
    free (timefpath);
  }
  if (strchr(melt_basename (binbase), '.'))
    melt_fatal_error ("MELT binary base %s to compile %s should not have dots", binbase, srcbase);
  if (strcmp(flavor, "quicklybuilt")
      && strcmp(flavor, "optimized")
      && strcmp(flavor, "debugnoline")
      && strcmp(flavor, "runextend"))
    melt_fatal_error ("invalid flavor %s to compile %s - expecting {quicklybuilt,optimized,debugnoline,runextend}", flavor, srcbase);
  ourmakecommand = melt_argument ("module-make-command");
  if (!ourmakecommand || !ourmakecommand[0])
    ourmakecommand = melt_module_make_command;
  melt_debugeprintf ("melt_compile_source ourmakecommand='%s'", ourmakecommand);
  gcc_assert (ourmakecommand[0]);
  ourmakefile = melt_argument ("module-makefile");
  if (!ourmakefile || !ourmakefile[0])
    ourmakefile = melt_module_makefile;
  melt_debugeprintf ("melt_compile_source ourmakefile: %s", ourmakefile);
  gcc_assert (ourmakefile[0]);
  ourcflags = melt_argument ("module-cflags");
  melt_debugeprintf ("melt_compile_source ourcflags from melt_argument:%s", ourcflags);
  if (!ourcflags || !ourcflags[0])
    ourcflags = melt_flag_bootstrapping?NULL
                :(getenv ("GCCMELT_MODULE_CFLAGS"));
  if (!ourcflags || !ourcflags[0])
    ourcflags = melt_module_cflags;
  melt_debugeprintf ("melt_compile_source ourcflags: %s", ourcflags);

  melt_debugeprintf ("melt_compile_source binbase='%s' srcbase='%s' flavor='%s'",
                     binbase, srcbase, flavor);
  gcc_assert (binbase != NULL && binbase[0] != (char)0);
  gcc_assert (srcbase != NULL && srcbase[0] != (char)0);
  gcc_assert (flavor != NULL && flavor[0] != (char)0);
  /* We use printf, not inform, because we are not sure that
     diagnostic buffers are flushed.  */
  printf ("\nMELT is building binary %s from source %s with flavor %s\n",
          binbase, srcbase, flavor);
  fflush (stdout);
  fflush (stderr);

#ifdef MELT_IS_PLUGIN
  melt_debugeprintf ("melt_compile_source before make/plugin flavor=%s srcbase=%s ourcflags=%s",
                     flavor, srcbase, ourcflags);
  fflush (NULL);
  melt_run_make_for_plugin (ourmakecommand, ourmakefile, ourcflags,
                            flavor, srcbase, binbase, workdir);
#else /* not MELT_IS_PLUGIN */
  melt_debugeprintf ("melt_compile_source before make/branch flavor=%s srcbase=%s ourcflags=%s",
                     flavor, srcbase, ourcflags);
  fflush (NULL);
  melt_run_make_for_branch (ourmakecommand, ourmakefile, ourcflags,
                            flavor, srcbase, binbase, workdir);
#endif /*MELT_IS_PLUGIN*/
  goto end;
end:
  melt_debugeprintf ("melt_compile_source end srcbase %s binbase %s flavor %s",
                     srcbase, binbase, flavor);
  MELT_EXITFRAME ();
}



/* compute the hexadecimal encoded md5sum string of a file into a given md5 hexbuf*/
static void
melt_string_hex_md5sum_file_to_hexbuf (const char* path, char md5hex[32])
{
#define MD5HEX_SIZE 32
  int ix = 0;
  char md5srctab[16];
  FILE *fil = NULL;
  memset (md5srctab, 0, sizeof (md5srctab));
  memset (md5hex, 0, MD5HEX_SIZE);
  if (!path || !path[0])
    return;
  fil = fopen(path, "r");
  if (!fil)
    return;
  if (md5_stream (fil, &md5srctab))
    melt_fatal_error
    ("failed to compute md5sum of file %s - %m",
     path);
  fclose (fil);
  fil = NULL;
  for (ix=0; ix<16; ix++)
    {
      char hexb[4] = {0,0,0,0};
      int curbyt = md5srctab[ix] & 0xff;
      snprintf (hexb, sizeof(hexb)-1, "%02x", curbyt);
      md5hex[2*ix] = hexb[0];
      md5hex[2*ix+1] = hexb[1];
    }
#undef MD5HEX_SIZE
}


melt_ptr_t
meltgc_string_hex_md5sum_file (const char* path)
{
  char hexbuf[48];    /* a bit longer than needed, to ensure null termination */
  MELT_ENTERFRAME(1, NULL);
#define resv meltfram__.mcfr_varptr[0]
  if (!path || !path[0])
    goto end;
  MELT_LOCATION_HERE("meltgc_string_hex_md5sum_file");
  memset (hexbuf, 0, sizeof(hexbuf));
  melt_string_hex_md5sum_file_to_hexbuf (path, hexbuf);
  if (!hexbuf[0])
    goto end;
  resv = meltgc_new_string ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),
                            hexbuf);
end:
  MELT_EXITFRAME();
  return (melt_ptr_t)resv;
#undef resv
}


/* compute the hexadecimal encoded md5sum string of a tuple of file
   paths, or NULL on failure.
   When we finish to proceed a file, we immediatly add the beginning of the
   following file to bufblock to keep a size of a multiple of 64.  This permit
   to call md5_process_block.  We only call md5_process_bytes for the last
   data.  */
melt_ptr_t
meltgc_string_hex_md5sum_file_sequence (melt_ptr_t pathtup_p)
{
  int ix = 0;
  char md5srctab[16];
  char md5hex[50];
  char bufblock[1024]; /* size should be multiple of 64 for md5_process_block */
  FILE *fil = NULL;
  int nbtup = 0;
  int cnt = 0;
  int new_file_cnt = 0;
  struct md5_ctx ctx;
  MELT_ENTERFRAME(3, NULL);
#define resv       meltfram__.mcfr_varptr[0]
#define pathtupv   meltfram__.mcfr_varptr[1]
#define pathv   meltfram__.mcfr_varptr[2]
  pathtupv = pathtup_p;
  memset (&ctx, 0, sizeof(ctx));
  memset (md5srctab, 0, sizeof (md5srctab));
  memset (md5hex, 0, sizeof (md5hex));
  if (melt_magic_discr ((melt_ptr_t)pathtupv) != MELTOBMAG_MULTIPLE)
    goto end;
  md5_init_ctx (&ctx);
  nbtup = melt_multiple_length ((melt_ptr_t)pathtupv);
  /* this loop does not garbage collect! */
  memset (bufblock, 0, sizeof (bufblock));
  for (ix=0; ix < nbtup; ix++)
    {
      const char *curpath = NULL;
      pathv = melt_multiple_nth ((melt_ptr_t)pathtupv, ix);
      if (melt_magic_discr ((melt_ptr_t)pathv) != MELTOBMAG_STRING)
        goto end;
      curpath = melt_string_str ((melt_ptr_t) pathv);
      if (!curpath || !curpath[0])
        goto end;
      fil = fopen(curpath, "r");
      if (!fil)
        goto end;
      while (!feof (fil))
        {
          if (cnt != 0) /*means that we havent process bufblock from previous
         file.*/
            {
              new_file_cnt =fread (bufblock+cnt, sizeof(char),sizeof(bufblock)-cnt, fil);
              cnt = cnt + new_file_cnt;

            }
          else
            {
              cnt = fread (bufblock, sizeof(char), sizeof(bufblock), fil);
            }
          if (cnt ==sizeof(bufblock))
            {
              /* an entire block has been read. */
              md5_process_block (bufblock, sizeof(bufblock), &ctx);
              memset (bufblock, '\0', sizeof (bufblock));
              cnt = 0;
            }
        }
      fclose (fil);
      fil = NULL;
      curpath = NULL;
    }
  if (cnt !=0)   /*We still have some data in the buffer*/
    {
      md5_process_bytes (bufblock, (size_t) cnt, &ctx);
    }
  md5_finish_ctx (&ctx, md5srctab);
  memset (md5hex, 0, sizeof(md5hex));
  for (ix=0; ix<16; ix++)
    {
      char hexb[4] = {0,0,0,0};
      int curbyt = md5srctab[ix] & 0xff;
      snprintf (hexb, sizeof(hexb)-1, "%02x", curbyt);
      md5hex[2*ix] = hexb[0];
      md5hex[2*ix+1] = hexb[1];
    }
  resv = meltgc_new_string ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING), md5hex);
end:
  MELT_EXITFRAME();
  return (melt_ptr_t)resv;
#undef resv
#undef pathtupv
#undef pathv
}

/* following code and comment is taken from the gcc/plugin.c file of
   the plugins branch */

/* We need a union to cast dlsym return value to a function pointer
   as ISO C forbids assignment between function pointer and 'void *'.
   Use explicit union instead of __extension__(<union_cast>) for
   portability.  */
#define PTR_UNION_TYPE(TOTYPE) union { void *_q; TOTYPE _nq; }
#define PTR_UNION_AS_VOID_PTR(NAME) (NAME._q)
#define PTR_UNION_AS_CAST_PTR(NAME) (NAME._nq)



void *
melt_dlsym_all (const char *nam)
{
  int ix = 0;
  int nbmod = Melt_Module::nb_modules();
  for (ix = 1; ix <= nbmod; ix++)
    {
      void* p = NULL;
      Melt_Module* cmod = Melt_Module::unsafe_nth_module (ix);
      gcc_assert (cmod != NULL && cmod->valid_magic());
      p = cmod->get_dlsym(nam);
      if (p)
        return p;
    };
  return (void *) dlsym (proghandle, nam);
}


/* Find a file path using either directories or colon-seperated paths,
   return a malloc-ed string or null. */
static char*melt_find_file_at (int line, const char*path, ...) ATTRIBUTE_SENTINEL;
#define MELT_FIND_FILE(PATH,...) melt_find_file_at (__LINE__,(PATH),__VA_ARGS__,NULL)

/* Option to find a file in a directory.  */
#define MELT_FILE_IN_DIRECTORY "directory"
/* Option to find a file in a colon-separated path.  */
#define MELT_FILE_IN_PATH "path"
/* Option to find a file in a colon-seperated path given by an environment variable.  */
#define MELT_FILE_IN_ENVIRON_PATH "envpath"
/* Option to log to some tracing file the findings of a file, should
   be first option to MELT_FIND_FILE.  */
#define MELT_FILE_LOG "log"


/* Called thru the MELT_FIND_FILE macro, returns a malloced string. */
static char*
melt_find_file_at (int lin, const char*path, ...)
{
  char* mode = NULL;
  FILE *logf = NULL;
  va_list args;
  if (!path)
    return NULL;
  va_start (args, path);
  while ((mode=va_arg (args, char*)) != NULL)
    {
      if (!strcmp(mode, MELT_FILE_IN_DIRECTORY))
        {
          char* fipath = NULL;
          char* indir = va_arg (args, char*);
          if (!indir || !indir[0])
            continue;
          fipath = concat (indir, "/", path, NULL);
          if (!access(fipath, R_OK))
            {
              if (logf)
                {
                  fprintf (logf, "found %s in directory %s\n", fipath, indir);
                  fflush (logf);
                }
              melt_debugeprintf ("found file %s in directory %s [%s:%d]",
                                 fipath, indir,
                                 melt_basename(__FILE__), lin);
              return fipath;
            }
          else if (logf && indir && indir[0])
            fprintf (logf, "not found in directory %s\n", indir);
          free (fipath);
        }
      else if (!strcmp(mode, MELT_FILE_IN_PATH))
        {
          char* inpath = va_arg(args, char*);
          char* dupinpath = NULL;
          char* pc = NULL;
          char* nextpc = NULL;
          char* col = NULL;
          char* fipath = NULL;
          if (!inpath || !inpath[0])
            continue;
          dupinpath = xstrdup (inpath);
          pc = dupinpath;
          for (pc = dupinpath; pc && *pc; pc = nextpc)
            {
              nextpc = NULL;
              col = strchr(pc, ':');
              if (col)
                {
                  *col = (char)0;
                  nextpc = col+1;
                }
              else
                col = pc + strlen(pc);
              fipath = concat (pc, "/", path, NULL);
              if (!access (fipath, R_OK))
                {
                  if (logf)
                    {
                      fprintf (logf, "found %s in colon path %s\n", fipath, inpath);
                      fflush (logf);
                    }
                  melt_debugeprintf ("found file %s in colon path %s [%s:%d]",
                                     fipath, inpath,
                                     melt_basename(__FILE__), lin);
                  free (dupinpath), dupinpath = NULL;
                  return fipath;
                }
            };
          if (logf)
            fprintf (logf, "not found in colon path %s\n", inpath);
          free (dupinpath), dupinpath = NULL;
        }
      else if (!strcmp(mode, MELT_FILE_IN_ENVIRON_PATH))
        {
          char* inenv = va_arg(args, char*);
          char* dupinpath = NULL;
          char* inpath = NULL;
          char* pc = NULL;
          char* nextpc = NULL;
          char* col = NULL;
          char* fipath = NULL;
          if (!inenv || !inenv[0])
            continue;
          inpath = getenv (inenv);
          if (!inpath || !inpath[0])
            {
              if (logf)
                fprintf (logf, "not found in path from unset environment variable %s\n", inenv);
              continue;
            };
          dupinpath = xstrdup (inpath);
          pc = dupinpath;
          for (pc = dupinpath; pc && *pc; pc = nextpc)
            {
              nextpc = NULL;
              col = strchr(pc, ':');
              if (col)
                {
                  *col = (char)0;
                  nextpc = col+1;
                }
              else
                col = pc + strlen(pc);
              fipath = concat (pc, "/", path, NULL);
              if (!access (fipath, R_OK))
                {
                  if (logf)
                    {
                      fprintf (logf, "found %s from environ %s in colon path %s\n", fipath, inenv, inpath);
                      fflush (logf);
                    }
                  melt_debugeprintf ("found file %s from environ %s in colon path %s [%s:%d]",
                                     fipath, inenv, inpath,
                                     melt_basename(__FILE__), lin);
                  free (dupinpath), dupinpath = NULL;
                  return fipath;
                }
            };
          if (logf)
            fprintf (logf, "not found from environ %s in colon path %s\n", inenv, inpath);
          free (dupinpath), dupinpath = NULL;
        }
      else if (!strcmp(mode, MELT_FILE_LOG))
        {
          logf = va_arg (args, FILE*);
          if (logf)
            fprintf (logf, "finding file %s [from %s:%d]\n",
                     path, melt_basename(__FILE__), lin);
          continue;
        }
      else
        melt_fatal_error ("MELT_FIND_FILE %s: bad mode %s [%s:%d]",
                          path, mode, melt_basename(__FILE__), lin);
    }
  va_end (args);
  if (logf)
    {
      fprintf (logf, "not found file %s [from %s:%d]\n",
               path, melt_basename(__FILE__), lin);
      fflush (logf);
    }
  melt_debugeprintf ("not found file %s [%s:%d]", path, melt_basename(__FILE__), lin);
  return NULL;
}





/*************** initial load machinery *******************/


#define MELT_READING_MAGIC 0xdeb73d1 /* 233534417 */
struct melt_reading_st
{
  unsigned readmagic;		/* always MELT_READING_MAGIC */
  FILE *rfil;
  const char *rpath;
  char *rcurlin;    /* current line mallocated buffer */
  int rlineno;      /* current line number */
  int rcol;     /* current column */
  source_location rsrcloc;  /* current source location */
  melt_ptr_t *rpfilnam;         /* pointer to location of file name string */
  bool rhas_file_location;  /* true iff the string comes from a file */
};

class melt_read_failure
  : public std::runtime_error
{
  struct melt_reading_st* _md;
  std::string _srcfile;
  int _srclineno;
  mutable char _whatbuf[128];
public:
  melt_read_failure(struct melt_reading_st*md,std::string file,int lineno)
    : std::runtime_error("MELT read error")
  {
    gcc_assert (md != NULL && md->readmagic == MELT_READING_MAGIC);
    _md = md;
    _srcfile = file;
    _srclineno = lineno;
    memset (_whatbuf, 0, sizeof(_whatbuf));
  };
  ~melt_read_failure() throw()
  {
    _md = NULL;
    _srcfile.clear();
    _srclineno = 0;
  }
  virtual const char* what() const throw()
  {
    const char* bnam = melt_basename (const_cast<const char*>(_srcfile.c_str()));
    snprintf (_whatbuf, sizeof(_whatbuf),
              "MELT read error from %s:%d",
              bnam, _srclineno);
    return const_cast<const char*>(_whatbuf);
  };
  const struct melt_reading_st*md() const
  {
    return _md;
  };
  const std::string srcfile() const
  {
    return _srcfile;
  };
  int srclineno() const
  {
    return _srclineno;
  };
};




#if MELT_HAVE_DEBUG

MELT_EXTERN bool melt_read_debug;
bool melt_read_debug;		/* to be set in gdb */
#define melt_dbgread_value(Msg,Val) do {		\
  if (melt_read_debug)					\
    melt_low_debug_value((Msg), (melt_ptr_t)(Val));	\
} while(0)

#define melt_dbgread_printf(Fmt,...) do {	\
    if (melt_read_debug)			\
      dbgprintf(Fmt,##__VA_ARGS__);		\
  } while(0)

#else

#define melt_dbgread_value(Msg,Val) ((void)(Val))
#define melt_dbgread_printf(Fmt,...) do{}while(0)
#endif	/* MELT_HAVE_DEBUG */

#define MELT_READ_TABULATION_FACTOR 8
/* Obstack used for reading strings */
static struct obstack melt_bstring_obstack;
#define rdback() (rd->rcol--)
#define rdnext() (rd->rcol++)
#define rdcurc() rd->rcurlin[rd->rcol]
#define rdfollowc(Rk) rd->rcurlin[rd->rcol + (Rk)]
#define rdeof() ((rd->rfil?feof(rd->rfil):1) && rd->rcurlin[rd->rcol]==0)

static void melt_linemap_compute_current_location (struct melt_reading_st *rd);

static void melt_read_got_error_at (struct melt_reading_st*rd, const char* file, int line)
throw (melt_read_failure);

#define MELT_READ_FAILURE(Fmt,...)    do {				\
   melt_linemap_compute_current_location (rd);				\
   error_at(rd->rsrcloc, Fmt, ##__VA_ARGS__);				\
   melt_read_got_error_at(rd, melt_basename(__FILE__), __LINE__);	\
} while(0)

#define MELT_READ_WARNING(Fmt,...)  do {			\
   melt_linemap_compute_current_location (rd);			\
   warning_at (rd->rsrcloc, 0, "MELT read warning: " Fmt,	\
                ##__VA_ARGS__);					\
 } while(0)

/* meltgc_readval returns the read value and sets *PGOT to true if something
   was read */
static melt_ptr_t meltgc_readval (struct melt_reading_st *rd, bool * pgot);

static void
melt_linemap_compute_current_location (struct melt_reading_st* rd)
{
  int colnum = 1;
  int cix = 0;
  if (!rd || !rd->rcurlin || !rd->rhas_file_location)
    return;
  gcc_assert (rd->readmagic == MELT_READING_MAGIC);
  for (cix=0; cix<rd->rcol; cix++)
    {
      char c = rd->rcurlin[cix];
      if (!c)
        break;
      else if (c == '\t')
        {
          while (colnum % MELT_READ_TABULATION_FACTOR != 0)
            colnum++;
        }
      else
        colnum++;
    }
  rd->rsrcloc = linemap_position_for_column (line_table, colnum);
}

static void
melt_read_got_error_at (struct melt_reading_st*rd, const char* file, int line)
throw (melt_read_failure)
{
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  error ("MELT read error from %s:%d [MELT built %s, version %s]",
         file, line, melt_runtime_build_date, melt_version_str ());
  if (rd->rpath && rd->rlineno && rd->rcol)
    error ("MELT read error while reading %s line %d column %d",
           rd->rpath, rd->rlineno, rd->rcol);
  fflush(NULL);
#if MELT_HAVE_DEBUG
  melt_dbgshortbacktrace ("MELT read error", 100);
#endif
  throw melt_read_failure(rd,std::string(file),line);
}

static melt_ptr_t meltgc_readstring (struct melt_reading_st *rd);
static melt_ptr_t meltgc_readmacrostringsequence (struct melt_reading_st *rd);

enum commenthandling_en
{ COMMENT_SKIP, COMMENT_NO };
static int
melt_skipspace_getc (struct melt_reading_st *rd, enum commenthandling_en comh)
{
  int c = 0;
  int incomm = 0;
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
readagain:
  if (rdeof ())
    return EOF;
  if (!rd->rcurlin)
    goto readline;
  c = rdcurc ();
  if ((c == '\n' && !rdfollowc (1)) || c == 0)
readline:
    {
      /* we expect most lines to fit into linbuf, so we don't handle
      efficiently long lines */
      static char linbuf[400];
      char *mlin = 0;   /* partial mallocated line buffer when
            not fitting into linbuf */
      char *eol = 0;
      if (!rd->rfil)    /* reading from a buffer: */
        {
          if (c)
            rdnext ();    /* Skip terminating newline. */
          return EOF;
        }
      if (rd->rcurlin)
        free ((void *) rd->rcurlin);
      rd->rcurlin = NULL;
      /* we really want getline here .... */
      for (;;)
        {
          memset (linbuf, 0, sizeof (linbuf));
          eol = NULL;
          if (!fgets (linbuf, sizeof (linbuf) - 2, rd->rfil))
            {
              /* reached eof, so either give mlin or duplicate an empty
              line */
              if (mlin)
                rd->rcurlin = mlin;
              else
                rd->rcurlin = xstrdup ("");
              break;
            }
          else
            eol = strchr (linbuf, '\n');
          if (eol)
            {
              if (rd->rcurlin)
                free ((void *) rd->rcurlin);
              if (!mlin)
                rd->rcurlin = xstrdup (linbuf);
              else
                {
                  rd->rcurlin = concat (mlin, linbuf, NULL);
                  free (mlin);
                }
              break;
            }
          else
            {
              /* read partly a long line without reaching the end of line */
              if (mlin)
                {
                  char *newmlin = concat (mlin, linbuf, NULL);
                  free (mlin);
                  mlin = newmlin;
                }
              else
                mlin = xstrdup (linbuf);
            }
        };
      rd->rlineno++;
      rd->rsrcloc =
        linemap_line_start (line_table, rd->rlineno, strlen (linbuf) + 1);
      rd->rcol = 0;
      if (comh == COMMENT_NO)
        return rdcurc();
      goto readagain;
    }
  /** The comment ;;## <linenum> [<filename>]
      is handled like #line, inspired by _cpp_do_file_change in
      libcpp/directives.c */
  else if (c == ';' && rdfollowc (1) == ';'
           && rdfollowc (2) == '#' && rdfollowc (3) == '#'
           && comh == COMMENT_SKIP)
    {
      char *endp = 0;
      char *newpath = 0;
      const char* newpathdup = 0;
      long newlineno = strtol (&rdfollowc (4), &endp, 10);
      /* take as filename from the first non-space to the last non-space */
      while (endp && *endp && ISSPACE(*endp)) endp++;
      if (endp && *endp) newpath=endp;
      if (endp && newpath) endp += strlen(endp) - 1;
      while (newpath && ISSPACE(*endp)) endp--;
      melt_debugeprintf (";;## directive for line newlineno=%ld newpath=%s",
                         newlineno, newpath);
      if (newlineno>0)
        {
          if (newpath)
            newpathdup = melt_intern_cstring (newpath);
          else
            newpathdup = melt_intern_cstring (rd->rpath);
        }
      (void) linemap_add (line_table, LC_RENAME_VERBATIM,
                          false, newpathdup, newlineno);
      goto readline;
    }
  else if (c == ';' && comh == COMMENT_SKIP)
    goto readline;

  else if (c == '#' && comh == COMMENT_SKIP && rdfollowc (1) == '|')
    {
      incomm = 1;
      rdnext ();
      c = rdcurc ();
      goto readagain;
    }
  else if (incomm && comh == COMMENT_SKIP && c == '|' && rdfollowc (1) == '#')
    {
      incomm = 0;
      rdnext ();
      rdnext ();
      c = rdcurc ();
      goto readagain;
    }
  else if (ISSPACE (c) || incomm)
    {
      rdnext ();
      c = rdcurc ();
      goto readagain;
    }
  else
    return c;
}


#define EXTRANAMECHARS "_+-*/<>=!?:%~&@$|"
/* read a simple name on the melt_bname_obstack */
static char *
melt_readsimplename (struct melt_reading_st *rd)
{
  int c = 0;
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  while (!rdeof () && (c = rdcurc ()) > 0 &&
         (ISALNUM (c) || strchr (EXTRANAMECHARS, c) != NULL))
    {
      obstack_1grow (&melt_bname_obstack, (char) c);
      rdnext ();
    }
  obstack_1grow (&melt_bname_obstack, (char) 0);
  return XOBFINISH (&melt_bname_obstack, char *);
}


/* read an integer, like +123, which may also be +%numbername */
static long
melt_readsimplelong (struct melt_reading_st *rd)
{
  int c = 0;
  long r = 0;
  char *endp = 0;
  char *nam = 0;
  bool neg = FALSE;
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  /* we do not need any GC locals ie MELT_ENTERFRAME because no
     garbage collection occurs here */
  c = rdcurc ();
  if (((c == '+' || c == '-') && ISDIGIT (rdfollowc (1))) || ISDIGIT (c))
    {
      /* R5RS and R6RS require decimal notation -since the binary and
      hex numbers are hash-prefixed but for convenience we accept
      them thru strtol */
      r = strtol (&rdcurc (), &endp, 0);
      if (r == 0 && endp <= &rdcurc ())
        MELT_READ_FAILURE ("MELT: failed to read number %.20s", &rdcurc ());
      rd->rcol += endp - &rdcurc ();
      return r;
    }
  else if ((c == '+' || c == '-') && rdfollowc (1) == '%')
    {
      neg = (c == '-');
      rdnext ();
      rdnext ();
      nam = melt_readsimplename (rd);
      r = -1;
      /* the +%magicname notation is seldom used, we don't care to do
      many needless strcmp-s in that case, to be able to define the
      below simple macro */
      if (!nam)
        MELT_READ_FAILURE
        ("MELT: magic number name expected after +%% or -%% for magic %s",
         nam);
#define NUMNAM(N) else if (!strcmp(nam,#N)) r = (N)
      NUMNAM (MELTOBMAG_OBJECT);
      NUMNAM (MELTOBMAG_MULTIPLE);
      NUMNAM (MELTOBMAG_CLOSURE);
      NUMNAM (MELTOBMAG_ROUTINE);
      NUMNAM (MELTOBMAG_LIST);
      NUMNAM (MELTOBMAG_PAIR);
      NUMNAM (MELTOBMAG_INT);
      NUMNAM (MELTOBMAG_MIXINT);
      NUMNAM (MELTOBMAG_MIXLOC);
      NUMNAM (MELTOBMAG_REAL);
      NUMNAM (MELTOBMAG_STRING);
      NUMNAM (MELTOBMAG_STRBUF);
      NUMNAM (MELTOBMAG_TREE);
      NUMNAM (MELTOBMAG_GIMPLE);
      NUMNAM (MELTOBMAG_GIMPLESEQ);
      NUMNAM (MELTOBMAG_BASICBLOCK);
      NUMNAM (MELTOBMAG_EDGE);
      NUMNAM (MELTOBMAG_MAPOBJECTS);
      NUMNAM (MELTOBMAG_MAPSTRINGS);
      NUMNAM (MELTOBMAG_MAPTREES);
      NUMNAM (MELTOBMAG_MAPGIMPLES);
      NUMNAM (MELTOBMAG_MAPGIMPLESEQS);
      NUMNAM (MELTOBMAG_MAPBASICBLOCKS);
      NUMNAM (MELTOBMAG_MAPEDGES);
      NUMNAM (MELTOBMAG_DECAY);
      NUMNAM (MELTOBMAG_SPECIAL_DATA);
      /** the fields' ranks of melt.h have been removed in rev126278 */
#undef NUMNAM
      if (r < 0)
        MELT_READ_FAILURE ("MELT: bad magic number name %s", nam);
      obstack_free (&melt_bname_obstack, nam);
      return neg ? -r : r;
    }
  else
    MELT_READ_FAILURE ("MELT: invalid number %.20s", &rdcurc ());
  return 0;
}


static melt_ptr_t
meltgc_readseqlist (struct melt_reading_st *rd, int endc)
{
  int c = 0;
  int nbcomp = 0;
  int startlin = rd->rlineno;
  bool got = FALSE;
  MELT_ENTERFRAME (4, NULL);
#define seqv  meltfram__.mcfr_varptr[0]
#define compv meltfram__.mcfr_varptr[1]
#define listv meltfram__.mcfr_varptr[2]
#define pairv meltfram__.mcfr_varptr[3]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
  melt_dbgread_value ("readseqlist start seqv=", seqv);
readagain:
  compv = NULL;
  c = melt_skipspace_getc (rd, COMMENT_SKIP);
  if (c == endc)
    {
      rdnext ();
      goto end;
    }
  else if (c == '}' && rdfollowc(1) == '#')
    {
      MELT_READ_FAILURE ("MELT: unexpected }# in s-expr sequence %.30s ... started line %d",
                         &rdcurc (), startlin);
    }
  /* The lexing ##{ ... }# is to insert a macrostring inside the
     current sequence. */
  else if (c == '#' && rdfollowc(1) == '#' && rdfollowc(2) == '{')
    {
      rdnext ();
      rdnext ();
      rdnext ();
      got = FALSE;
      compv = meltgc_readmacrostringsequence (rd);
      if (melt_is_instance_of ((melt_ptr_t) compv, MELT_PREDEF (CLASS_SEXPR)))
        {
          got = TRUE;
          listv = melt_field_object ((melt_ptr_t)compv, MELTFIELD_SEXP_CONTENTS);
          if (melt_magic_discr ((melt_ptr_t)listv) == MELTOBMAG_LIST)
            {
              compv = NULL;
              for (pairv = (melt_ptr_t) ((struct meltlist_st*)(listv))->first;
                   pairv && melt_magic_discr((melt_ptr_t)pairv) == MELTOBMAG_PAIR;
                   pairv = (melt_ptr_t) ((struct meltpair_st*)(pairv))->tl)
                {
                  compv = (melt_ptr_t) ((struct meltpair_st*)(pairv))->hd;
                  if (compv)
                    {
                      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
                      nbcomp++;
                    }
                }
            }
        }
      if (!got)
        MELT_READ_FAILURE ("MELT: unexpected stuff in macrostring seq %.20s ... started line %d",
                           &rdcurc (), startlin);
      goto readagain;
    }
  got = FALSE;
  compv = meltgc_readval (rd, &got);
  if (!compv && !got)
    MELT_READ_FAILURE ("MELT: unexpected stuff in seq %.20s ... started line %d",
                       &rdcurc (), startlin);
  meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
  nbcomp++;
  goto readagain;
end:
  melt_dbgread_value ("readseqlist end seqv=", seqv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) seqv;
#undef compv
#undef seqv
#undef listv
#undef pairv
}




enum melt_macrostring_en
{
  MELT_MACSTR_PLAIN=0,
  MELT_MACSTR_MACRO
};

static melt_ptr_t
meltgc_makesexpr (struct melt_reading_st *rd, int lineno, melt_ptr_t contents_p,
                  location_t loc, enum melt_macrostring_en ismacrostring)
{
  MELT_ENTERFRAME (4, NULL);
#define sexprv  meltfram__.mcfr_varptr[0]
#define contsv   meltfram__.mcfr_varptr[1]
#define locmixv meltfram__.mcfr_varptr[2]
#define sexpclassv meltfram__.mcfr_varptr[3]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  contsv = contents_p;
  melt_dbgread_value ("readmakesexpr start contsv=", contsv);
  gcc_assert (melt_magic_discr ((melt_ptr_t) contsv) == MELTOBMAG_LIST);
  if (loc == 0)
    locmixv = meltgc_new_mixint ((meltobject_ptr_t) MELT_PREDEF (DISCR_MIXED_INTEGER),
                                 *rd->rpfilnam, (long) lineno);
  else
    locmixv = meltgc_new_mixloc ((meltobject_ptr_t) MELT_PREDEF (DISCR_MIXED_LOCATION),
                                 *rd->rpfilnam, (long) lineno, loc);
  if (ismacrostring == MELT_MACSTR_MACRO  && (MELT_PREDEF (CLASS_SEXPR_MACROSTRING)))
    sexpclassv = MELT_PREDEF (CLASS_SEXPR_MACROSTRING);
  else
    sexpclassv = MELT_PREDEF (CLASS_SEXPR);
  sexprv = (melt_ptr_t) meltgc_new_raw_object ((meltobject_ptr_t) (sexpclassv),
           MELTLENGTH_CLASS_SEXPR);
  ((meltobject_ptr_t) (sexprv))->obj_vartab[MELTFIELD_LOCA_LOCATION] =
    (melt_ptr_t) locmixv;
  ((meltobject_ptr_t) (sexprv))->obj_vartab[MELTFIELD_SEXP_CONTENTS] =
    (melt_ptr_t) contsv;
  meltgc_touch (sexprv);
  melt_dbgread_value ("readmakesexpr end sexprv=", sexprv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) sexprv;
#undef sexprv
#undef contsv
#undef locmixv
#undef sexpclassv
}


static melt_ptr_t
meltgc_readsexpr (struct melt_reading_st *rd, int endc)
{
  int lineno = rd->rlineno;
  location_t loc = 0;
  char curlocbuf[100];
  curlocbuf[0] = 0;
  MELT_ENTERFRAME (3, NULL);
#define sexprv  meltfram__.mcfr_varptr[0]
#define contv   meltfram__.mcfr_varptr[1]
#define locmixv meltfram__.mcfr_varptr[2]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  if (!endc || rdeof ())
    MELT_READ_FAILURE ("MELT: eof in s-expr (lin%d)", lineno);
  (void) melt_skipspace_getc (rd, COMMENT_SKIP);
  melt_linemap_compute_current_location (rd);
  loc = rd->rsrcloc;
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "readsexpr @ %s:%d:%d",
                             melt_basename(LOCATION_FILE(loc)),
                             LOCATION_LINE (loc), LOCATION_COLUMN(loc));
  contv = meltgc_readseqlist (rd, endc);
  sexprv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) contv, loc, MELT_MACSTR_PLAIN);
  melt_dbgread_value ("readsexpr sexprv=", sexprv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) sexprv;
#undef sexprv
#undef contv
#undef locmixv
}




/* if the string ends with "_ call gettext on it to have it
   localized/internationlized -i18n- */
static melt_ptr_t
meltgc_readstring (struct melt_reading_st *rd)
{
  int c = 0;
  int nbesc = 0;
  char *cstr = 0, *endc = 0;
  bool isintl = false;
  MELT_ENTERFRAME (1, NULL);
#define strv   meltfram__.mcfr_varptr[0]
#define str_strv  ((struct meltstring_st*)(strv))
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  obstack_init (&melt_bstring_obstack);
  while ((c = rdcurc ()) != '"' && !rdeof ())
    {
      if (c != '\\')
        {
          obstack_1grow (&melt_bstring_obstack, (char) c);
          if (c == '\n')
            {
              /* It is suspicious when a double-quote is parsed as the
              last character of a line; issue a warning in that
              case.  This helps to catch missing, mismatched or
              extra double-quotes! */
              if (obstack_object_size (&melt_bstring_obstack) <= 1)
                warning_at (rd->rsrcloc, 0, "suspicious MELT string starting at end of line");
              c = melt_skipspace_getc (rd, COMMENT_NO);
              continue;
            }
          else
            rdnext ();
        }
      else
        {
          rdnext ();
          c = rdcurc ();
          nbesc++;
          switch (c)
            {
            case 'a':
              c = '\a';
              rdnext ();
              break;
            case 'b':
              c = '\b';
              rdnext ();
              break;
            case 't':
              c = '\t';
              rdnext ();
              break;
            case 'n':
              c = '\n';
              rdnext ();
              break;
            case 'v':
              c = '\v';
              rdnext ();
              break;
            case 'f':
              c = '\f';
              rdnext ();
              break;
            case 'r':
              c = '\r';
              rdnext ();
              break;
            case '"':
              c = '\"';
              rdnext ();
              break;
            case '\\':
              c = '\\';
              rdnext ();
              break;
            case '\n':
            case '\r':
              melt_skipspace_getc (rd, COMMENT_NO);
              continue;
            case ' ':
              c = ' ';
              rdnext ();
              break;
            case 'x':
              rdnext ();
              c = (char) strtol (&rdcurc (), &endc, 16);
              if (c == 0 && endc <= &rdcurc ())
                MELT_READ_FAILURE ("MELT: illegal hex \\x escape in string %.20s",
                                   &rdcurc ());
              if (*endc == ';')
                endc++;
              rd->rcol += endc - &rdcurc ();
              break;
            case '{':
            {
              int linbrac = rd->rlineno;
              /* the escaped left brace \{ read verbatim all the string till the right brace } */
              rdnext ();
              while (rdcurc () != '}')
                {
                  int cc;
                  if (rdeof ())
                    MELT_READ_FAILURE
                    ("MELT: reached end of file in braced block string starting line %d",
                     linbrac);
                  cc = rdcurc ();
                  if (cc == '\n')
                    cc = melt_skipspace_getc (rd, COMMENT_NO);
                  else
                    obstack_1grow (&melt_bstring_obstack, (char) cc);
                  rdnext ();
                };
              rdnext ();
            }
            break;
            default:
              MELT_READ_FAILURE
              ("MELT: illegal escape sequence %.10s in string -- got \\%c (hex %x)",
               &rdcurc () - 1, c, c);
            }
          obstack_1grow (&melt_bstring_obstack, (char) c);
        }
    }
  if (c == '"')
    rdnext ();
  else
    MELT_READ_FAILURE ("MELT: unterminated string %.20s", &rdcurc ());
  c = rdcurc ();
  if (c == '_' && !rdeof ())
    {
      isintl = true;
      rdnext ();
    }
  obstack_1grow (&melt_bstring_obstack, (char) 0);
  cstr = XOBFINISH (&melt_bstring_obstack, char *);
  if (isintl)
    cstr = gettext (cstr);
  strv = meltgc_new_string ((meltobject_ptr_t) MELT_PREDEF (DISCR_STRING), cstr);
  obstack_free (&melt_bstring_obstack, cstr);
  melt_dbgread_value ("readstring strv=", strv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) strv;
#undef strv
#undef str_strv
}



melt_ptr_t
meltgc_strbuf_json_string_peek (melt_ptr_t v, int ioff, int*pendoff)
{
  int curix = 0;
  int slen = 0;
  MELT_ENTERFRAME (3, NULL);
#define bufv   meltfram__.mcfr_varptr[0]
#define sbuf_bufv ((struct meltstrbuf_st *)(bufv))
#define unsafe_sbuf_nth(N) (sbuf_bufv->bufzn+sbuf_bufv->bufstart+(N))
#define wbuv   meltfram__.mcfr_varptr[1]
#define sbuf_wbuv ((struct meltstrbuf_st *)(wbuv))
#define resv   meltfram__.mcfr_varptr[2]
  bufv = v;
  resv = NULL;
  gcc_assert (pendoff != NULL);
  if (melt_magic_discr (bufv) != MELTOBMAG_STRBUF)
    goto end;
  slen = (sbuf_bufv->bufend) - (sbuf_bufv->bufstart);
  if (ioff<0)
    ioff += slen;
  if (ioff < 0 || ioff >= slen-1)
    goto end;
  if (*unsafe_sbuf_nth(ioff) != '"')
    goto end;
  {
    char* ndqu = strchr(unsafe_sbuf_nth(ioff+1), '"');
    if (!ndqu) goto end;
    int clen = ndqu-unsafe_sbuf_nth(ioff);
    wbuv = (melt_ptr_t)
           meltgc_new_strbuf ((meltobject_ptr_t)MELT_PREDEF(DISCR_STRBUF), NULL);
    meltgc_strbuf_reserve(wbuv, 9*clen/8+20);
  }
  curix = ioff+1;
  while (curix < slen)
    {
      char cc = *unsafe_sbuf_nth(curix);
      if (ISALNUM(cc) || cc=='_' || ISSPACE(cc))
        {
          meltgc_add_strbuf_raw_len(wbuv,&cc,1);
          curix++;
          continue;
        }
      else if (cc=='"')
        {
          curix++;
          *pendoff = curix;
          resv = meltgc_new_stringdup((meltobject_ptr_t)MELT_PREDEF(DISCR_STRING),melt_strbuf_str(wbuv));
          goto end;
        }
      else if (cc=='\\' && curix+1<slen)
        {
          /* see http://www.json.org/ */
          char ec = *unsafe_sbuf_nth(curix+1);
          curix++;
          switch (ec)
            {
#define ADD1S(S) meltgc_add_strbuf_raw_len(wbuv,S,1); curix++; break
            case '"':
              ADD1S("\"");
            case '\\':
              ADD1S("\\");
            case '/':
              ADD1S("/");
            case 'b':
              ADD1S("\b");
            case 'f':
              ADD1S("\f");
            case 'n':
              ADD1S("\n");
            case 'r':
              ADD1S("\r");
            case 't':
              ADD1S("\r");
#undef ADD1S
            case 'u':
              if (curix+6<slen)
                {
                  char hbuf[8] = {0};
                  memset (hbuf, 0, sizeof(hbuf));
                  char ubuf[8] = {0};
                  memset (ubuf, 0, sizeof(ubuf));
                  if (ISXDIGIT(*unsafe_sbuf_nth(curix+2))) hbuf[0] = *unsafe_sbuf_nth(curix+2);
                  if (ISXDIGIT(*unsafe_sbuf_nth(curix+3))) hbuf[1] = *unsafe_sbuf_nth(curix+3);
                  if (ISXDIGIT(*unsafe_sbuf_nth(curix+4))) hbuf[2] = *unsafe_sbuf_nth(curix+4);
                  if (ISXDIGIT(*unsafe_sbuf_nth(curix+5))) hbuf[3] = *unsafe_sbuf_nth(curix+5);
                  curix+=5;
                  uint32_t uc = (uint32_t)strtol(hbuf, NULL, 16);
                  // I am slightly tempted to use
                  // http://www.gnu.org/software/libunistring/ but I really
                  // want to avoid adding some additional dependencies.
                  /// some code inspired by http://tidy.sourceforge.net/cgi-bin/lxr/source/src/utf8.c
                  if (uc <= 0x7f)  	 // one UTF8 byte
                    {
                      ubuf[0] = uc;
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,1);
                    }
                  else if (uc <= 0x7ff)   // two UTF8 bytes
                    {
                      ubuf[0] = ( 0xC0 | (uc >> 6) );
                      ubuf[1] = ( 0x80 | (uc & 0x3F) );
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,2);
                    }
                  else if (uc <= 0xffff)   // three UTF8 bytes
                    {
                      // we don't care checking about UTF surrogates...
                      ubuf[0] =  (0xE0 | (uc >> 12));
                      ubuf[1] = (0x80 | ((uc >> 6) & 0x3F));
                      ubuf[2] = (0x80 | (uc & 0x3F));
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,3);
                    }
                  else if (uc <=  0x1FFFFF)   // four UTF8 bytes
                    {
                      // don't care about strange errors
                      ubuf[0] =  (0xF0 | (uc >> 18));
                      ubuf[1] =  (0x80 | ((uc >> 12) & 0x3F));
                      ubuf[2] =  (0x80 | ((uc >> 6) & 0x3F));
                      ubuf[3] =  (0x80 | (uc & 0x3F));
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,4);
                    }
                  else if (uc <= 0x3FFFFFF)  // five UTF8 bytes
                    {
                      ubuf[0] =  (0xF8 | (uc >> 24));
                      ubuf[1] =  (0x80 | (uc >> 18));
                      ubuf[2] =  (0x80 | ((uc >> 12) & 0x3F));
                      ubuf[3] =  (0x80 | ((uc >> 6) & 0x3F));
                      ubuf[4] =  (0x80 | (uc & 0x3F));
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,5);
                    }
                  else if (uc <= 0x7FFFFFFF)  //  six UTF8 bytes
                    {
                      ubuf[0] =  (0xFC | (uc >> 30));
                      ubuf[1] =  (0x80 | ((uc >> 24) & 0x3F));
                      ubuf[2] =  (0x80 | ((uc >> 18) & 0x3F));
                      ubuf[3] =  (0x80 | ((uc >> 12) & 0x3F));
                      ubuf[4] =  (0x80 | ((uc >> 6) & 0x3F));
                      ubuf[5] =  (0x80 | (uc & 0x3F));
                      meltgc_add_strbuf_raw_len(wbuv,ubuf,6);
                    }
                }
              break;
            default:
              meltgc_add_strbuf_raw_len(wbuv,&ec,1);
              curix++;
              break;
            }
        }
      else if (cc != '\0') /* some other character */
        {
          meltgc_add_strbuf_raw_len(wbuv,&cc,1);
          curix++;
          continue;
        }
      else  // reached end of buffer
        goto end;
    };
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef bufv
#undef wbuv
#undef resv
#undef sbuf_bufv
#undef unsafe_sbuf_nth
#undef sbuf_wbuv
}





/**
   macrostring so #{if ($A>0) printf("%s", $B);}# is parsed as would
   be parsed the s-expr ("if (" A ">0) printf(\"%s\", " B ");")

   read a macrostring sequence starting with #{ and ending with }#
   perhaps spanning several lines in the source no escape characters
   are handled (in particular no backslash escapes) except the dollar
   sign $ and then ending }#

   A $ followed by alphabetical caracters (or as in C by underscores
   or digits, provided the first is not a digit) is handled as a
   symbol. If it is immediately followed by an hash # the # is
   skipped

   A $ followed by a left parenthesis ( is read as an embedded
   S-expression, it should end with a balanced right parenthesis )

   A $ followed by a left square bracket [ is read as a embedded
   sequence of S-epxressions, it should end with a balanced right
   square bracket ]

   $MELT_SOURCE_FILE is expanded at *read time* to the source file name
   $MELT_SOURCE_LINE is expanded at *read time* to the source line number

**/
static melt_ptr_t
meltgc_readmacrostringsequence (struct melt_reading_st *rd)
{
  int lineno = rd->rlineno;
  int escaped = 0;
  int quoted = 0;
  location_t loc = 0;
  char curlocbuf[100];
  curlocbuf[0] = 0;
  MELT_ENTERFRAME (8, NULL);
#define readv    meltfram__.mcfr_varptr[0]
#define strv     meltfram__.mcfr_varptr[1]
#define symbv    meltfram__.mcfr_varptr[2]
#define seqv     meltfram__.mcfr_varptr[3]
#define sbufv    meltfram__.mcfr_varptr[4]
#define compv    meltfram__.mcfr_varptr[5]
#define subseqv  meltfram__.mcfr_varptr[6]
#define pairv    meltfram__.mcfr_varptr[7]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  melt_linemap_compute_current_location (rd);
  loc = rd->rsrcloc;
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "readmacrostringsequence @ %s:%d:%d",
                             melt_basename(LOCATION_FILE(loc)),
                             LOCATION_LINE (loc), LOCATION_COLUMN(loc));
  melt_dbgread_printf("readmacrostringsequence curlocbuf=%s", curlocbuf);
  seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
  sbufv = (melt_ptr_t) meltgc_new_strbuf((meltobject_ptr_t) MELT_PREDEF(DISCR_STRBUF), (char*)0);
  if (rdcurc() == '$' && rdfollowc(1)=='\'')
    {
      symbv = melthookproc_HOOK_NAMED_SYMBOL ("quote", (long)MELT_CREATE);
      quoted = 1;
      meltgc_append_list((melt_ptr_t) seqv, (melt_ptr_t) symbv);
      symbv = NULL;
      rdnext ();
      rdnext ();
    }
  for(;;)
    {
      if (rdeof())
        MELT_READ_FAILURE("reached end of file in macrostring sequence started line %d; a }# is probably missing.",
                          lineno);
      if (!rdcurc())
        {
          /* reached end of line */
          melt_skipspace_getc(rd, COMMENT_NO);
          continue;
        }
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readmacrostringsequence inside @ %s:%d:%d;",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_value (curlocbuf, seqv);

#define melt_macrostring_flush_sbufv() do {                             \
	if (sbufv && melt_strbuf_usedlength((melt_ptr_t)sbufv)>0) {	\
	  strv = meltgc_new_stringdup					\
	    ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),		\
	     melt_strbuf_str((melt_ptr_t) sbufv));			\
	  melt_dbgread_value ("readmacrostringsequence strv=", strv);	\
	  meltgc_append_list((melt_ptr_t) seqv,				\
			     (melt_ptr_t) strv);			\
	  if (!escaped							\
	      && strstr (melt_string_str ((melt_ptr_t) strv), "}#"))	\
	    warning_at(rd->rsrcloc, 0,					\
		       "MELT macrostring starting at line %d"		\
		       " containing }# might be suspicious",		\
		       lineno);						\
	  if (!escaped							\
	      && strstr (melt_string_str ((melt_ptr_t) strv), "#{"))	\
	    warning_at(rd->rsrcloc, 0,					\
		       "MELT macrostring starting at line %d"		\
		       " containing #{ might be suspicious",		\
		       lineno);						\
	  strv = NULL;							\
	  sbufv = NULL;							\
	}								\
      } while(0)

      if (rdcurc()=='}' && rdfollowc(1)=='#')
        {
          melt_macrostring_flush_sbufv ();
          rdnext ();
          rdnext ();

          break;
        }
      else if (rdcurc()=='$')
        {
          /* $ followed by letters or underscore makes a symbol */
          if (ISALPHA(rdfollowc(1)) || rdfollowc(1)=='_')
            {
              int lnam = 1;
              char tinybuf[80];
              memset(tinybuf, 0, sizeof(tinybuf));
              melt_macrostring_flush_sbufv ();
              symbv = NULL;
              gcc_assert(sizeof(tinybuf)-1 >= sizeof(MELT_MAGICSYMB_FILE));
              gcc_assert(sizeof(tinybuf)-1 >= sizeof(MELT_MAGICSYMB_LINE));
              while (ISALNUM(rdfollowc(lnam)) || rdfollowc(lnam) == '_')
                lnam++;
              if (lnam< (int)sizeof(tinybuf)-2)
                {
                  memcpy(tinybuf, &rdfollowc(1), lnam-1);
                  for (int ix=0; ix<lnam; ix++)
                    if (ISLOWER(tinybuf[lnam]))
                      tinybuf[lnam] = TOUPPER(tinybuf[lnam]);
                  tinybuf[lnam] = (char)0;
                  // handle the magic symbols _MELT_FILE_ & _MELT_LINE_ to expand
                  // them at read time to the file name and the line number
                  // respectively
                  if (MELT_UNLIKELY(tinybuf[0] == '_' && tinybuf[1] == 'M'))
                    {
                      if (!strcmp(tinybuf, MELT_MAGICSYMB_FILE))
                        symbv = (*rd->rpfilnam)?(*rd->rpfilnam):MELT_PREDEF(UNKNOWN_LOCATION);
                      else if (!strcmp(tinybuf, MELT_MAGICSYMB_LINE))
                        symbv = meltgc_new_int((meltobject_ptr_t) MELT_PREDEF(DISCR_INTEGER),
                                               rd->rlineno);
                    };
                  if (MELT_LIKELY(!symbv))
                    {
                      if (quoted)
                        MELT_READ_WARNING ("quoted macro string with $%s symbol", tinybuf);
                      symbv = melthookproc_HOOK_NAMED_SYMBOL(tinybuf, (long) MELT_CREATE);
                    }
                }
              else
                {
                  char *nambuf = (char*) xcalloc(lnam+2, 1);
                  memcpy(nambuf, &rdfollowc(1), lnam-1);
                  nambuf[lnam] = (char)0;
                  symbv = melthookproc_HOOK_NAMED_SYMBOL(nambuf, (long) MELT_CREATE);
                  if (quoted)
                    MELT_READ_WARNING ("quoted macro string with $%s symbol", nambuf);
                  free(nambuf);
                }
              rd->rcol += lnam;
              /* skip the hash # if just after the symbol */
              if (rdcurc() == '#')
                rdnext();
              /* append the symbol */
              meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) symbv);;
              melt_dbgread_value ("readmacrostringsequence symbv=", symbv);
              symbv = NULL;
            }
          /* $. is silently skipped */
          else if (rdfollowc(1) == '.')
            {
              escaped = 1;
              rdnext();
              rdnext();
            }
          /* $$ is handled as a single dollar $ */
          else if (rdfollowc(1) == '$')
            {
              if (!sbufv)
                sbufv = (melt_ptr_t) meltgc_new_strbuf((meltobject_ptr_t) MELT_PREDEF(DISCR_STRBUF), (char*)0);
              meltgc_add_strbuf_raw_len((melt_ptr_t)sbufv, "$", 1);
              rdnext();
              rdnext();
            }
          /* $# is handled as a single hash # */
          else if (rdfollowc(1) == '#')
            {
              escaped = 1;
              if (!sbufv)
                sbufv = (melt_ptr_t) meltgc_new_strbuf((meltobject_ptr_t) MELT_PREDEF(DISCR_STRBUF), (char*)0);
              meltgc_add_strbuf_raw_len((melt_ptr_t)sbufv, "#", 1);
              rdnext();
              rdnext();
            }
          /* $(some s-expr) is acceptable to embed a single s-expression */
          else if (rdfollowc(1) == '(')
            {
              melt_macrostring_flush_sbufv ();
              rdnext ();
              rdnext ();
              compv = meltgc_readsexpr (rd, ')');
              melt_dbgread_value ("readmacrostringsequence sexpr compv=", compv);
              /* append the s-expr */
              meltgc_append_list((melt_ptr_t) seqv, (melt_ptr_t) compv);
              compv = NULL;
            }
          /* $[several sub-expr] is acceptable to embed a sequence of s-expressions */
          else if (rdfollowc(1) == '[')
            {
              melt_macrostring_flush_sbufv ();
              rdnext ();
              rdnext ();
              subseqv = meltgc_readseqlist(rd, ']');
              if (melt_magic_discr ((melt_ptr_t)subseqv) == MELTOBMAG_LIST)
                {
                  compv = NULL;
                  for (pairv = (melt_ptr_t) ((struct meltlist_st*)(subseqv))->first;
                       pairv && melt_magic_discr((melt_ptr_t)pairv) == MELTOBMAG_PAIR;
                       pairv = (melt_ptr_t) ((struct meltpair_st*)(pairv))->tl)
                    {
                      compv = (melt_ptr_t) ((struct meltpair_st*)(pairv))->hd;
                      if (compv)
                        {
                          meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
                          melt_dbgread_value ("readmacrostringsequence sexpr compv=", compv);
                        }
                    }
                  pairv = NULL;
                  compv = NULL;
                }
            }
          /* any other dollar something is an error */
          else MELT_READ_FAILURE("unexpected dollar escape in macrostring %.4s started line %d",
                                   &rdcurc(), lineno);
        }
      else if ( ISALNUM(rdcurc()) || ISSPACE(rdcurc()) )
        {
          /* handle efficiently the common case of alphanum and spaces */
          int nbc = 0;
          if (!sbufv)
            sbufv = (melt_ptr_t) meltgc_new_strbuf((meltobject_ptr_t) MELT_PREDEF(DISCR_STRBUF), (char*)0);
          while (ISALNUM(rdfollowc(nbc)) || ISSPACE(rdfollowc(nbc)))
            nbc++;
          meltgc_add_strbuf_raw_len((melt_ptr_t)sbufv, &rdcurc(), nbc);
          rd->rcol += nbc;
        }
      else
        {
          /* the current char is not a dollar $ nor an alnum */
          /* if the macro string contains #{ it is suspicious. */
          if (rdcurc() == '#' && rdfollowc(1) == '{')
            warning_at(rd->rsrcloc, 0,
                       "internal #{ inside MELT macrostring starting at line %d might be suspicious", lineno);
          if (!sbufv)
            sbufv = (melt_ptr_t) meltgc_new_strbuf((meltobject_ptr_t) MELT_PREDEF(DISCR_STRBUF), (char*)0);
          meltgc_add_strbuf_raw_len((melt_ptr_t)sbufv, &rdcurc(), 1);
          rdnext();
        }
    }
  melt_macrostring_flush_sbufv ();
  readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_MACRO);
  melt_dbgread_value("readmacrostringsequence result=", readv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) readv;
#undef melt_macrostring_flush_sbufv
#undef readv
#undef strv
#undef symbv
#undef seqv
#undef sbufv
#undef compv
#undef subseqv
#undef pairv
}


static melt_ptr_t
meltgc_readhashescape (struct melt_reading_st *rd)
{
  int c = 0;
  char *nam = NULL;
  int lineno = rd->rlineno;
  MELT_ENTERFRAME (4, NULL);
#define readv  meltfram__.mcfr_varptr[0]
#define compv  meltfram__.mcfr_varptr[1]
#define listv  meltfram__.mcfr_varptr[2]
#define pairv  meltfram__.mcfr_varptr[3]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  readv = NULL;
  c = rdcurc ();
  if (!c || rdeof ())
    MELT_READ_FAILURE ("MELT: eof in hashescape %.20s starting line %d", &rdcurc (), lineno);
  if (c == '\\')
    {
      rdnext ();
      if (ISALPHA (rdcurc ()) && rdcurc () != 'x' && ISALPHA (rdfollowc (1)))
        {
          nam = melt_readsimplename (rd);
          c = 0;
          if (!strcmp (nam, "nul"))
            c = 0;
          else if (!strcmp (nam, "alarm"))
            c = '\a';
          else if (!strcmp (nam, "backspace"))
            c = '\b';
          else if (!strcmp (nam, "tab"))
            c = '\t';
          else if (!strcmp (nam, "linefeed"))
            c = '\n';
          else if (!strcmp (nam, "vtab"))
            c = '\v';
          else if (!strcmp (nam, "page"))
            c = '\f';
          else if (!strcmp (nam, "return"))
            c = '\r';
          else if (!strcmp (nam, "space"))
            c = ' ';
          /* won't work on non ASCII or ISO or Unicode host, but we don't care */
          else if (!strcmp (nam, "delete"))
            c = 0xff;
          else if (!strcmp (nam, "esc"))
            c = 0x1b;
          else
            MELT_READ_FAILURE ("MELT: invalid char escape %s starting line %d", nam, lineno);
          obstack_free (&melt_bname_obstack, nam);
char_escape:
          readv = meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_CHARACTER_INTEGER), c);
          melt_dbgread_value ("readhashescape readv=", readv);
        }
      else if (rdcurc () == 'x' && ISXDIGIT (rdfollowc (1)))
        {
          char *endc = 0;
          rdnext ();
          c = strtol (&rdcurc (), &endc, 16);
          if (c == 0 && endc <= &rdcurc ())
            MELT_READ_FAILURE ("MELT: illegal hex #\\x escape in char %.20s starting line %d",
                               &rdcurc (), lineno);
          rd->rcol += endc - &rdcurc ();
          goto char_escape;
        }
      else if (ISPRINT (rdcurc ()))
        {
          c = rdcurc ();
          rdnext ();
          goto char_escape;
        }
      else
        MELT_READ_FAILURE ("MELT: unrecognized char escape #\\%s starting line %d",
                           &rdcurc (), lineno);
    }
  else if (c == '(')
    {
      int ln = 0, ix = 0;
      listv = meltgc_readseqlist (rd, ')');
      melt_dbgread_value ("readhashescape listv=", listv);
      ln = melt_list_length ((melt_ptr_t) listv);
      gcc_assert (ln >= 0);
      readv = meltgc_new_multiple ((meltobject_ptr_t) MELT_PREDEF (DISCR_MULTIPLE), ln);
      for ((ix = 0), (pairv =
                        (melt_ptr_t) ((struct meltlist_st *) (listv))->first);
           ix < ln
           && melt_magic_discr ((melt_ptr_t) pairv) == MELTOBMAG_PAIR;
           pairv = (melt_ptr_t) ((struct meltpair_st *) (pairv))->tl)
        ((struct meltmultiple_st *) (readv))->tabval[ix++] =
          ((struct meltpair_st *) (pairv))->hd;
      meltgc_touch (readv);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if (c == '[')
    {
      /* a melt extension #[ .... ] for lists */
      readv = meltgc_readseqlist (rd, ']');
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if ((c == 'b' || c == 'B') && ISDIGIT (rdfollowc (1)))
    {
      /* binary number */
      char *endc = 0;
      long n = 0;
      rdnext ();
      n = strtol (&rdcurc (), &endc, 2);
      if (n == 0 && endc <= &rdcurc ())
        MELT_READ_FAILURE ("MELT: bad binary number %s starting line %d", endc, lineno);
      readv = meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_INTEGER), n);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if ((c == 'o' || c == 'O') && ISDIGIT (rdfollowc (1)))
    {
      /* octal number */
      char *endc = 0;
      long n = 0;
      rdnext ();
      n = strtol (&rdcurc (), &endc, 8);
      if (n == 0 && endc <= &rdcurc ())
        MELT_READ_FAILURE ("MELT: bad octal number %s starting line %d", endc, lineno);
      readv = meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_INTEGER), n);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if ((c == 'd' || c == 'D') && ISDIGIT (rdfollowc (1)))
    {
      /* decimal number */
      char *endc = 0;
      long n = 0;
      rdnext ();
      n = strtol (&rdcurc (), &endc, 10);
      if (n == 0 && endc <= &rdcurc ())
        MELT_READ_FAILURE ("MELT: bad decimal number %s starting line %d", endc, lineno);
      readv = meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_INTEGER), n);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if ((c == 'x' || c == 'x') && ISDIGIT (rdfollowc (1)))
    {
      /* hex number */
      char *endc = 0;
      long n = 0;
      rdnext ();
      n = strtol (&rdcurc (), &endc, 16);
      if (n == 0 && endc <= &rdcurc ())
        MELT_READ_FAILURE ("MELT: bad octal number %s starting line %d", endc, lineno);
      readv = meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_INTEGER), n);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else if (c == '+' && ISALPHA (rdfollowc (1)))
    {
      bool gotcomp = FALSE;
      char *nam = 0;
      nam = melt_readsimplename (rd);
      compv = meltgc_readval (rd, &gotcomp);
      if (!strcmp (nam, "MELT"))
        readv = compv;
      else
        readv = meltgc_readval (rd, &gotcomp);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  /* #{ is a macrostringsequence; it is terminated by }# and each
      occurrence of $ followed by alphanum char is considered as a
      MELT symbol, the other caracters are considered as string
      chunks; the entire read is a sequence */
  else if (c == '{')
    {
      rdnext ();
      readv = meltgc_readmacrostringsequence(rd);
      melt_dbgread_value ("readhashescape readv=", readv);
    }
  else
    MELT_READ_FAILURE ("MELT: invalid escape %.20s starting line %d", &rdcurc (), lineno);
  melt_dbgread_value ("readhashescape final readv=", readv);
  MELT_EXITFRAME ();
  return (melt_ptr_t) readv;
#undef readv
#undef listv
#undef compv
#undef pairv
}



static melt_ptr_t
meltgc_readval (struct melt_reading_st *rd, bool * pgot)
{
  int c = 0;
  char *nam = 0;
  int lineno = rd->rlineno;
  location_t loc = 0;
  char curlocbuf[120];
  curlocbuf[0] = 0;
  MELT_ENTERFRAME (4, NULL);
#define readv   meltfram__.mcfr_varptr[0]
#define compv   meltfram__.mcfr_varptr[1]
#define seqv    meltfram__.mcfr_varptr[2]
#define altv    meltfram__.mcfr_varptr[3]
  gcc_assert (rd && rd->readmagic == MELT_READING_MAGIC);
  loc = rd->rsrcloc;
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "readvalstart @ %s:%d:%d;",
                             melt_basename(LOCATION_FILE(loc)),
                             LOCATION_LINE (loc), LOCATION_COLUMN(loc));
  melt_dbgread_printf("readval start curlocbuf=%s", curlocbuf);
  readv = NULL;
  c = melt_skipspace_getc (rd, COMMENT_SKIP);
  /*   melt_debugeprintf ("start meltgc_readval line %d col %d char %c", rd->rlineno, rd->rcol,
  ISPRINT (c) ? c : ' '); */
  if (ISDIGIT (c)
      || ((c == '-' || c == '+')
          && (ISDIGIT (rdfollowc (1)) || rdfollowc (1) == '%'
              || rdfollowc (1) == '|')))
    {
      long num = 0;
      num = melt_readsimplelong (rd);
      readv =
        meltgc_new_int ((meltobject_ptr_t) MELT_PREDEF (DISCR_INTEGER),
                        num);
      melt_dbgread_value ("readval number readv=", readv);
      *pgot = TRUE;
      goto end;
    }       /* end if ISDIGIT or '-' or '+' */
  else if (c == '"')
    {
      rdnext ();
      readv = meltgc_readstring (rd);
      melt_dbgread_value ("readval string readv=", readv);
      *pgot = TRUE;
      goto end;
    }       /* end if '"' */
  else if (c == '(')
    {
      rdnext ();
      if (rdcurc () == ')')
        {
          rdnext ();
          readv = NULL;
          *pgot = TRUE;
          melt_dbgread_value ("readval nil readv=", readv);
          goto end;
        }
      readv = meltgc_readsexpr (rd, ')');
      melt_dbgread_value ("readval sexpr readv=", readv);
      *pgot = TRUE;
      goto end;
    }       /* end if '(' */
  else if (c == ')')
    {
      readv = NULL;
      *pgot = FALSE;
      MELT_READ_FAILURE ("MELT: unexpected closing parenthesis %.20s", &rdcurc ());
      goto end;
    }
  else if (c == '[')
    {
      rdnext ();
      readv = meltgc_readsexpr (rd, ']');
      melt_dbgread_value ("readval sexpr readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '#')
    {
      rdnext ();
      c = rdcurc ();
      readv = meltgc_readhashescape (rd);
      melt_dbgread_value ("readval hashescape readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '\'')
    {
      bool got = false;
      rdnext ();
      compv = meltgc_readval (rd, &got);
      melt_dbgread_value ("readval quote compv=", compv);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after quote %.20s", &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("quote", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      melt_dbgread_value ("readval altv=", altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      melt_dbgread_value ("readval compv=", compv);
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval quote @ %s:%d:%d;",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval quotesexpr curlocbuf=%s", curlocbuf);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval quotesexpr readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '!'
           && (ISALPHA (rdfollowc (1)) || ISSPACE (rdfollowc (1))
               || rdfollowc (1) == '('))
    {
      bool got = false;
      location_t loc = 0;
      rdnext ();
      compv = meltgc_readval (rd, &got);
      melt_dbgread_value ("readval exclaim compv=", compv);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after exclamation mark ! %.20s", &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("exclaim", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval exclaim @ %s:%d:%d",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval exclaimsexpr curlocbuf=%s", curlocbuf);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval exclaimsexpr readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '`')
    {
      bool got = false;
      location_t loc = 0;
      rdnext ();
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval backquote @ %s:%d:%d",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval backquote curlocbuf=%s", curlocbuf);
      compv = meltgc_readval (rd, &got);
      melt_dbgread_value ("readval backquote compv=", compv);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after backquote %.20s",
                           &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("backquote", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval backquote readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == ',')
    {
      bool got = false;
      location_t loc = 0;
      rdnext ();
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval comma @ %s:%d:%d",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval comma curlocbuf=%s", curlocbuf);
      compv = meltgc_readval (rd, &got);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after comma %.20s", &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("comma", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval comma readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '@')
    {
      bool got = false;
      location_t loc = 0;
      rdnext ();
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval at @ %s:%d:%d",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval atsign curlocbuf=%s", curlocbuf);
      compv = meltgc_readval (rd, &got);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after at %.20s", &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("at", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval atsign readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == '?')
    {
      bool got = false;
      location_t loc = 0;
      rdnext ();
      melt_linemap_compute_current_location (rd);
      loc = rd->rsrcloc;
      MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                 "readval question @ %s:%d:%d",
                                 melt_basename(LOCATION_FILE(loc)),
                                 LOCATION_LINE (loc), LOCATION_COLUMN(loc));
      melt_dbgread_printf("readval question curlocbuf=%s", curlocbuf);
      compv = meltgc_readval (rd, &got);
      if (!got)
        MELT_READ_FAILURE ("MELT: expecting value after question %.20s", &rdcurc ());
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      altv = melthookproc_HOOK_NAMED_SYMBOL ("question", (long) MELT_CREATE);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) altv);
      meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) compv);
      readv = meltgc_makesexpr (rd, lineno, (melt_ptr_t) seqv, loc, MELT_MACSTR_PLAIN);
      melt_dbgread_value ("readval question readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else if (c == ':')
    {
      readv = NULL;
      if (!ISALPHA (rdfollowc(1)))
        MELT_READ_FAILURE ("MELT: colon should be followed by letter for keyword, but got %c",
                           rdfollowc(1));
      nam = melt_readsimplename (rd);
      readv = melthookproc_HOOK_NAMED_KEYWORD (nam, (long)MELT_CREATE);
      melt_dbgread_value ("readval keyword readv=", readv);
      if (!readv)
        MELT_READ_FAILURE ("MELT: unknown named keyword %s", nam);
      *pgot = TRUE;
      goto end;
    }
  else if (ISALPHA (c) || strchr (EXTRANAMECHARS, c) != NULL)
    {
      readv = NULL;
      nam = melt_readsimplename (rd);
      // handle the magic symbols _MELT_FILE_ and _MELT_LINE_ to
      // expand them to the file name and the line number respectively
      // at read time!
      if (MELT_UNLIKELY(((nam[0]=='_') && (nam[1]=='M' || nam[1]=='M'))))
        {
          if (!strcasecmp(nam, MELT_MAGICSYMB_FILE))
            readv = (*rd->rpfilnam)?(*rd->rpfilnam):MELT_PREDEF(UNKNOWN_LOCATION);
          else if (!strcasecmp(nam,  MELT_MAGICSYMB_LINE))
            readv = meltgc_new_int((meltobject_ptr_t) MELT_PREDEF(DISCR_INTEGER),
                                   rd->rlineno);
        }
      if (!readv)
        readv = melthookproc_HOOK_NAMED_SYMBOL (nam, (long) MELT_CREATE);
      melt_dbgread_value ("readval symbol readv=", readv);
      *pgot = TRUE;
      goto end;
    }
  else
    {
      if (c >= 0)
        rdback ();
      readv = NULL;
    }
end:
  MELT_EXITFRAME ();
  if (nam)
    {
      *nam = 0;
      obstack_free (&melt_bname_obstack, nam);
    };
  melt_dbgread_value ("readval final readv=", readv);
  return (melt_ptr_t) readv;
#undef readv
#undef compv
#undef seqv
#undef altv
}


/* This function gets the source location and the filename -as a
   memoized string value- and the line number of a location bearing
   value... */
static location_t
meltgc_retrieve_location_from_value (melt_ptr_t loc_p,
                                     melt_ptr_t* filename_pp=NULL, int* lineno_ptr=NULL)
{
  location_t resloc = UNKNOWN_LOCATION;
  int magic = 0;
  int lineno = 0;
  MELT_ENTERFRAME (2, NULL);
#define locv       meltfram__.mcfr_varptr[0]
#define filenamev  meltfram__.mcfr_varptr[1]
  locv = loc_p;
  filenamev = NULL;
  if (!locv)
    goto end;
  magic = melt_magic_discr (locv);
  switch (magic)
    {
    case MELTOBMAG_MIXLOC:
      resloc = melt_location_mixloc ((melt_ptr_t) locv);
      filenamev = melt_val_mixloc ((melt_ptr_t) locv);
      lineno = melt_num_mixloc ((melt_ptr_t) locv);
      break;
    case MELTOBMAG_MIXINT:
      resloc = UNKNOWN_LOCATION;
      filenamev = melt_val_mixint ((melt_ptr_t) locv);
      lineno = melt_num_mixint ((melt_ptr_t) locv);
      break;
    case MELTOBMAG_GIMPLE:
    {
      melt_gimpleptr_t g = melt_gimple_content ((melt_ptr_t) locv);
      resloc = g?gimple_location(g):UNKNOWN_LOCATION;
      break;
    }
    case MELTOBMAG_GIMPLESEQ:
    {
      melt_gimpleseqptr_t gs = melt_gimpleseq_content ((melt_ptr_t) locv);
      melt_gimpleptr_t g = gs?gimple_seq_first_stmt(gs):NULL;
      resloc = g?gimple_location(g):UNKNOWN_LOCATION;
      break;
    }
    case MELTOBMAG_TREE:
    {
      tree tr = melt_tree_content((melt_ptr_t) locv);
      if (tr)
        resloc =
          DECL_P(tr) ? DECL_SOURCE_LOCATION(tr)
          : EXPR_P(tr) ? EXPR_LOCATION(tr)
          : UNKNOWN_LOCATION;
      break;
    }
    default:
      break;
    };
  if (resloc)
    {
      if (filename_pp && !filenamev)
        filenamev = meltgc_cached_string_path_of_source_location (resloc);
      lineno = LOCATION_LINE (resloc);
    }
end:
  if (filename_pp)
    *filename_pp = filenamev;
  if (lineno_ptr)
    *lineno_ptr = lineno;
  MELT_EXITFRAME ();
  return resloc;
#undef locv
#undef filenamev
#undef linenov
}

void
melt_error_str (melt_ptr_t mixloc_p, const char *msg,
                melt_ptr_t str_p)
{
  int lineno = 0;
  location_t loc = UNKNOWN_LOCATION;
  MELT_ENTERFRAME (3, NULL);
#define mixlocv    meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define finamv     meltfram__.mcfr_varptr[2]
  gcc_assert (msg && msg[0]);
  melt_error_counter ++;
  mixlocv = mixloc_p;
  strv = str_p;
  loc = meltgc_retrieve_location_from_value (mixlocv, &finamv, &lineno);
  if (loc)
    {
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cstr)
        {
          if (melt_dbgcounter > 0)
            error_at (loc, "MELT error [#%ld]: %s - %s", melt_dbgcounter,
                      msg, cstr);
          else
            error_at (loc, "MELT error: %s - %s",
                      msg, cstr);
        }
      else
        {
          if (melt_dbgcounter > 0)
            error_at (loc, "MELT error [#%ld]: %s", melt_dbgcounter, msg);
          else
            error_at (loc, "MELT error: %s",msg);
        }
    }
  else
    {
      const char *cfilnam = melt_string_str ((melt_ptr_t) finamv);
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cfilnam)
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                error ("MELT error [#%ld] @ %s:%d: %s - %s", melt_dbgcounter,
                       cfilnam, lineno, msg, cstr);
              else
                error ("MELT error @ %s:%d: %s - %s",
                       cfilnam, lineno, msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                error ("MELT error [#%ld] @ %s:%d: %s", melt_dbgcounter,
                       cfilnam, lineno, msg);
              else
                error ("MELT error @ %s:%d: %s",
                       cfilnam, lineno, msg);
            }
        }
      else
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                error ("MELT error [#%ld]: %s - %s", melt_dbgcounter, msg,
                       cstr);
              else
                error ("MELT error: %s - %s", msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                error ("MELT error [#%ld]: %s", melt_dbgcounter, msg);
              else
                error ("MELT error: %s", msg);
            }
        }
    }
  MELT_EXITFRAME ();
}

#undef mixlocv
#undef strv
#undef finamv


void melt_warning_at_strbuf (location_t loc, melt_ptr_t msgbuf)
{
  char *str;
  if (!msgbuf || melt_magic_discr (msgbuf) != MELTOBMAG_STRBUF)
    return;
  str = xstrndup (melt_strbuf_str (msgbuf),
                  (size_t) melt_strbuf_usedlength(msgbuf));
  if(str == NULL)
    return;
  if (melt_dbgcounter > 0)
    warning_at (loc, /*no OPT_*/0, "MELT warning[#%ld]: %s",
                melt_dbgcounter, str);
  else
    warning_at (loc, /*no OPT_*/0, "MELT warning: %s", str);
  free (str);
}


void
melt_warning_str (int opt, melt_ptr_t mixloc_p, const char *msg,
                  melt_ptr_t str_p)
{
  int lineno = 0;
  location_t loc = 0;
  MELT_ENTERFRAME (3, NULL);
#define mixlocv    meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define finamv     meltfram__.mcfr_varptr[2]
  gcc_assert (msg && msg[0]);
  mixlocv = mixloc_p;
  strv = str_p;
  loc = meltgc_retrieve_location_from_value (mixlocv, &finamv, &lineno);
  if (loc)
    {
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cstr)
        {
          if (melt_dbgcounter > 0)
            warning_at (loc, opt, "MELT warning [#%ld]: %s - %s",
                        melt_dbgcounter, msg, cstr);
          else
            warning_at (loc, opt, "MELT warning: %s - %s",
                        msg, cstr);
        }
      else
        {
          if (melt_dbgcounter > 0)
            warning_at (loc, opt, "MELT warning [#%ld]: %s",
                        melt_dbgcounter, msg);
          else
            warning_at (loc, opt, "MELT warning: %s",
                        msg);
        }
    }
  else
    {
      const char *cfilnam = melt_string_str ((melt_ptr_t) finamv);
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cfilnam)
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                warning (opt, "MELT warning [#%ld] @ %s:%d: %s - %s",
                         melt_dbgcounter, cfilnam, lineno, msg, cstr);
              else
                warning (opt, "MELT warning @ %s:%d: %s - %s",
                         cfilnam, lineno, msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                warning (opt, "MELT warning [#%ld] @ %s:%d: %s",
                         melt_dbgcounter, cfilnam, lineno, msg);
              else
                warning (opt, "MELT warning @ %s:%d: %s",
                         cfilnam, lineno, msg);
            }
        }
      else
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                warning (opt, "MELT warning [#%ld]: %s - %s",
                         melt_dbgcounter, msg, cstr);
              else
                warning (opt, "MELT warning: %s - %s",
                         msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                warning (opt, "MELT warning [#%ld]: %s", melt_dbgcounter,
                         msg);
              else
                warning (opt, "MELT warning: %s", msg);
            }

        }
    }
  MELT_EXITFRAME ();
#undef mixlocv
#undef strv
#undef finamv
}




void
melt_inform_str (melt_ptr_t mixloc_p, const char *msg,
                 melt_ptr_t str_p)
{
  int lineno = 0;
  location_t loc = 0;
  MELT_ENTERFRAME (3, NULL);
#define mixlocv    meltfram__.mcfr_varptr[0]
#define strv       meltfram__.mcfr_varptr[1]
#define finamv     meltfram__.mcfr_varptr[2]
  gcc_assert (msg && msg[0]);
  mixlocv = mixloc_p;
  strv = str_p;
  loc = meltgc_retrieve_location_from_value (mixlocv, &finamv, &lineno);
  if (loc)
    {
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cstr)
        {
          if (melt_dbgcounter > 0)
            inform (loc, "MELT inform [#%ld]: %s - %s", melt_dbgcounter,
                    msg, cstr);
          else
            inform (loc, "MELT inform: %s - %s",
                    msg, cstr);
        }
      else
        {
          if (melt_dbgcounter > 0)
            inform (loc, "MELT inform [#%ld]: %s", melt_dbgcounter, msg);
          else
            inform (loc, "MELT inform: %s", msg);
        }
    }
  else
    {
      const char *cfilnam = melt_string_str ((melt_ptr_t) finamv);
      const char *cstr = melt_string_str ((melt_ptr_t) strv);
      if (cfilnam)
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                inform (UNKNOWN_LOCATION, "MELT inform [#%ld] @ %s:%d: %s - %s",
                        melt_dbgcounter, cfilnam, lineno, msg, cstr);
              else
                inform (UNKNOWN_LOCATION, "MELT inform @ %s:%d: %s - %s",
                        cfilnam, lineno, msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                inform (UNKNOWN_LOCATION, "MELT inform [#%ld] @ %s:%d: %s",
                        melt_dbgcounter, cfilnam, lineno, msg);
              else
                inform (UNKNOWN_LOCATION, "MELT inform @ %s:%d: %s",
                        cfilnam, lineno, msg);
            }
        }
      else
        {
          if (cstr)
            {
              if (melt_dbgcounter > 0)
                inform (UNKNOWN_LOCATION, "MELT inform [#%ld]: %s - %s",
                        melt_dbgcounter, msg, cstr);
              else
                inform (UNKNOWN_LOCATION, "MELT inform: %s - %s",
                        msg, cstr);
            }
          else
            {
              if (melt_dbgcounter > 0)
                inform (UNKNOWN_LOCATION, "MELT inform [#%ld]: %s",
                        melt_dbgcounter, msg);
              else
                inform (UNKNOWN_LOCATION, "MELT inform: %s", msg);
            }
        }
    }
  MELT_EXITFRAME ();
#undef mixlocv
#undef strv
#undef finamv
}





melt_ptr_t
meltgc_read_file (const char *filnam, const char *locnam)
{
  char curlocbuf[140];
  memset (curlocbuf, 0, sizeof(curlocbuf));
  struct melt_reading_st rds;
  FILE *fil = NULL;
  struct melt_reading_st *rd = NULL;
  const char *filnamdup = NULL;
  const char* srcpathstr = melt_argument ("source-path");
  MELT_ENTERFRAME (3, NULL);
#define valv      meltfram__.mcfr_varptr[0]
#define seqv      meltfram__.mcfr_varptr[1]
#define locnamv   meltfram__.mcfr_varptr[2]
  memset (&rds, 0, sizeof (rds));
  melt_debugeprintf ("meltgc_read_file filnam %s locnam %s", filnam, locnam);
  if (!filnam || !filnam[0])
    goto end;
  if (!locnam || !locnam[0])
    locnam = melt_basename (filnam);
  if (melt_trace_source_fil)
    {
      fprintf (melt_trace_source_fil, "MELT reads MELT source file %s, locally %s\n", filnam, locnam);
      fflush (melt_trace_source_fil);
    }
  filnamdup = melt_intern_cstring (filnam);
  melt_debugeprintf ("meltgc_read_file filnamdup %s locnam %s", filnamdup, locnam);
  if (!strcmp (filnamdup, "-"))
    fil = stdin;
  else
    fil = fopen (filnamdup, "rt");
  /* If needed, find the file in the source path.  */
  if (!fil && !IS_ABSOLUTE_PATH(filnam))
    {
      filnamdup =
        MELT_FIND_FILE (filnam,
                        MELT_FILE_LOG, melt_trace_source_fil,
                        MELT_FILE_IN_PATH, srcpathstr,
                        MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_SOURCE_PATH",
                        MELT_FILE_IN_DIRECTORY, melt_flag_bootstrapping?NULL:melt_source_dir,
                        NULL);
      melt_debugeprintf ("meltgc_read_file filenamdup %s", filnamdup);
      if (filnamdup)
        fil = fopen (filnamdup, "rt");
      if (fil)
        filnamdup = melt_intern_cstring (filnamdup);
    }
  if (!fil)
    {
      if (filnam && srcpathstr)
        inform (UNKNOWN_LOCATION,
                "didn't found MELT file %s with source path %s",
                filnam, srcpathstr);
      if (getenv("GCCMELT_SOURCE_PATH"))
        inform (UNKNOWN_LOCATION,
                "MELT tried from GCCMELT_SOURCE_PATH=%s environment variable",
                getenv("GCCMELT_SOURCE_PATH"));
      inform (UNKNOWN_LOCATION, "builtin MELT source directory is %s",
              melt_source_dir);
      if (melt_trace_source_fil)
        fflush (melt_trace_source_fil);
      else
        inform (UNKNOWN_LOCATION,
                "You could set the GCCMELT_TRACE_SOURCE env.var. to some file path for debugging");
      melt_fatal_error ("cannot open MELT source file %s - %m", filnam);
    }
  /* Warn if the filename is not stdin and has strange characters in its base name,
     notably + */
  if (strcmp(filnamdup, "-"))
    {
      const char* filbase = 0;
      int warn = 0;
      for (filbase = melt_basename (filnamdup); *filbase; filbase++)
        {
          if (ISALNUM (*filbase) || *filbase=='-'
              || *filbase=='_' || *filbase=='.')
            continue;
          warn = 1;
        }
      if (warn)
        warning (0, "MELT file name %s has strange characters", filnamdup);

    }
  MELT_LOCATION_HERE_PRINTF (curlocbuf, "meltgc_read_file start reading %s", filnamdup);
  /*  melt_debugeprintf ("starting loading file %s", filnamdup); */
  rds.rfil = fil;
  rds.rpath = filnamdup;
  rds.rlineno = 0;
  (void) linemap_add (line_table, LC_ENTER, false, filnamdup, 0);
  locnamv = meltgc_new_stringdup ((meltobject_ptr_t) MELT_PREDEF (DISCR_STRING), locnam);
  rds.rpfilnam = (melt_ptr_t *) & locnamv;
  rds.rhas_file_location = true;
  rds.readmagic = MELT_READING_MAGIC;
  try
    {
      rd = &rds;
      seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
      while (!rdeof ())
        {
          bool got = FALSE;
          location_t loc = 0;
          melt_skipspace_getc (rd, COMMENT_SKIP);
          if (rdeof ())
            break;
          loc = rd->rsrcloc;
          MELT_LOCATION_HERE_PRINTF (curlocbuf,
                                     "meltgc_read_file @ %s:%d:%d",
                                     melt_basename(LOCATION_FILE(loc)),
                                     LOCATION_LINE (loc), LOCATION_COLUMN(loc));
          melt_dbgread_printf("read_file curlocbuf=%s", curlocbuf);
          valv = meltgc_readval (rd, &got);
          melt_dbgread_value("read_file valv=", valv);
          if (!got)
            MELT_READ_FAILURE ("MELT: no value read %.20s", &rdcurc ());
          meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) valv);
        };
      if (rds.rfil)
        fclose (rds.rfil);
      linemap_add (line_table, LC_LEAVE, false, NULL, 0);
      memset (&rds, 0, sizeof(rds));
    }
  catch (melt_read_failure readerr)
    {
      warning (0, "MELT reading of file %s failed line %d col %d - %s",
               filnamdup, rds.rlineno, rds.rcol, readerr.what());
      seqv = NULL;
      goto end;
    }
  rd = 0;
end:
  melt_dbgread_value("read_file seqv=", seqv);
  if (!seqv)
    {
      melt_debugeprintf ("meltgc_read_file filnam %s fail & return NULL", filnamdup);
      warning(0, "MELT file %s read without content, perhaps failed.", filnamdup);
    }
  else
    melt_debugeprintf ("meltgc_read_file filnam %s return list of %d elem",
                       filnamdup, melt_list_length ((melt_ptr_t) seqv));
  MELT_EXITFRAME ();
  return (melt_ptr_t) seqv;
#undef vecshv
#undef locnamv
#undef seqv
#undef valv
}


melt_ptr_t
meltgc_read_from_rawstring (const char *rawstr, const char *locnam,
                            location_t loch)
{
  char curlocbuf[140];
  memset (curlocbuf, 0, sizeof(curlocbuf));
  struct melt_reading_st rds;
  char *rbuf = 0;
  struct melt_reading_st *rd = 0;
  MELT_ENTERFRAME (3, NULL);
#define seqv      meltfram__.mcfr_varptr[0]
#define locnamv   meltfram__.mcfr_varptr[1]
#define valv      meltfram__.mcfr_varptr[2]
  memset (&rds, 0, sizeof (rds));
  if (!rawstr)
    goto end;
  rbuf = xstrdup (rawstr);
  rds.rfil = 0;
  rds.rpath = 0;
  rds.rlineno = 0;
  rds.rcurlin = rbuf;
  rds.rsrcloc = loch;
  rds.rhas_file_location = false;
  rd = &rds;
  if (locnam)
    {
      locnamv = meltgc_new_stringdup ((meltobject_ptr_t) MELT_PREDEF (DISCR_STRING), locnam);
      MELT_LOCATION_HERE_PRINTF(curlocbuf, "meltgc_read_from_rawstring locnam=%s", locnam);
    }
  else
    {
      static long bufcount;
      char locnambuf[64];
      bufcount++;
      snprintf (locnambuf, sizeof (locnambuf), "<string-buffer-%ld>", bufcount);
      locnamv = meltgc_new_string ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),
                                   locnambuf);
      MELT_LOCATION_HERE_PRINTF(curlocbuf, "meltgc_read_from_rawstring rawstr=%.50s", rawstr);
    }
  seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
  rds.readmagic = MELT_READING_MAGIC;
  rds.rpfilnam = (melt_ptr_t *) & locnamv;
  try
    {
      while (rdcurc ())
        {
          bool got = FALSE;
          melt_skipspace_getc (rd, COMMENT_SKIP);
          if (!rdcurc () || rdeof ())
            break;
          valv = meltgc_readval (rd, &got);
          if (!got)
            MELT_READ_FAILURE ("MELT: no value read %.20s", &rdcurc ());
          meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) valv);
        };
    }
  catch (melt_read_failure readerr)
    {
      warning (0, "MELT reading of string %s line %d col %d failed - %s",
               melt_string_str ((melt_ptr_t) locnamv), rd->rlineno, rd->rcol, readerr.what());
      seqv = NULL;
      goto end;
    }
  rd = 0;
  free (rbuf);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) seqv;
#undef vecshv
#undef seqv
#undef locnamv
#undef valv
}


melt_ptr_t
meltgc_read_from_val (melt_ptr_t strv_p, melt_ptr_t locnam_p)
{
  static long parsecount;
  char curlocbuf[140];
  memset (curlocbuf, 0, sizeof(curlocbuf));
  struct melt_reading_st rds;
  char *rbuf = 0;
  struct melt_reading_st *rd = 0;
  int strmagic = 0;
  MELT_ENTERFRAME (4, NULL);
#define valv      meltfram__.mcfr_varptr[0]
#define locnamv   meltfram__.mcfr_varptr[1]
#define seqv      meltfram__.mcfr_varptr[2]
#define strv      meltfram__.mcfr_varptr[3]
  memset (&rds, 0, sizeof (rds));
  strv = strv_p;
  locnamv = locnam_p;
  rbuf = 0;
  strmagic = melt_magic_discr ((melt_ptr_t) strv);
  seqv = meltgc_new_list ((meltobject_ptr_t) MELT_PREDEF (DISCR_LIST));
  switch (strmagic)
    {
    case MELTOBMAG_STRING:
      rbuf = (char *) xstrdup (melt_string_str ((melt_ptr_t) strv));
      break;
    case MELTOBMAG_STRBUF:
      rbuf = xstrdup (melt_strbuf_str ((melt_ptr_t) strv));
      break;
    case MELTOBMAG_OBJECT:
      if (melt_is_instance_of
          ((melt_ptr_t) strv, (melt_ptr_t) MELT_PREDEF (CLASS_NAMED)))
        strv = melt_object_nth_field ((melt_ptr_t) strv, MELTFIELD_NAMED_NAME);
      else
        strv = NULL;
      if (melt_string_str ((melt_ptr_t) strv))
        rbuf = xstrdup (melt_string_str ((melt_ptr_t) strv));
      break;
    default:
      break;
    }
  if (!rbuf)
    goto end;
  parsecount++;
  rds.rfil = 0;
  rds.rpath = 0;
  rds.rlineno = 0;
  rds.rcurlin = rbuf;
  rds.rhas_file_location = false;
  rd = &rds;
  rds.readmagic = MELT_READING_MAGIC;
  try
    {
      MELT_LOCATION_HERE_PRINTF(curlocbuf, "meltgc_read_from_val rbuf=%.70s", rbuf);
      if (locnamv == NULL || melt_magic_discr(locnamv) != MELTOBMAG_STRING)
        {
          char buf[40];
          memset(buf, 0, sizeof(buf));
          snprintf (buf, sizeof(buf), "<parsed-string#%ld>", parsecount);
          locnamv = meltgc_new_string ((meltobject_ptr_t) MELT_PREDEF(DISCR_STRING),
                                       buf);
        }
      rds.rpfilnam = (melt_ptr_t *) & locnamv;
      while (rdcurc ())
        {
          bool got = FALSE;
          melt_skipspace_getc (rd, COMMENT_SKIP);
          if (!rdcurc () || rdeof ())
            break;
          valv = meltgc_readval (rd, &got);
          if (!got)
            MELT_READ_FAILURE ("MELT: no value read %.20s", &rdcurc ());
          meltgc_append_list ((melt_ptr_t) seqv, (melt_ptr_t) valv);
        };
    }
  catch (melt_read_failure readerr)
    {
      warning (0, "MELT reading from value line %d col %d failed - %s",
               rd->rlineno, rd->rcol, readerr.what());
      seqv = NULL;
      goto end;
    }

  rd = 0;
  free (rbuf);
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) seqv;
#undef vecshv
#undef locnamv
#undef seqv
#undef strv
#undef valv
}







/* the plugin callback to register melt attributes */
static void
melt_attribute_callback (void *gcc_data ATTRIBUTE_UNUSED,
                         void* user_data ATTRIBUTE_UNUSED)
{
  melt_debugeprintf("melt_attribute_callback before HOOK_INSTALL_ATTRIBUTES user_data@%p", user_data);
  melthookproc_HOOK_INSTALL_ATTRIBUTES ();
  melt_debugeprintf("melt_attribute_callback after HOOK_INSTALL_ATTRIBUTES user_data@%p", user_data);
}


int
melt_predefined_index_by_name (const char* pname)
{
  if (!pname || !pname[0]) return 0;
#define MELT_HAS_PREDEFINED(Nam,Ix) if (!strcasecmp (pname, #Nam)) return Ix;
#include "melt-predef.h"
  return 0;
}








static void melt_do_finalize (void);


/* the plugin callback when finishing all */
static void
melt_finishall_callback(void *gcc_data ATTRIBUTE_UNUSED,
                        void* user_data ATTRIBUTE_UNUSED)
{
  melt_debugeprintf ("melt_finishall_callback melt_nb_garbcoll=%ld", melt_nb_garbcoll);
  melt_do_finalize ();
}




/* Utility function to parse a C-encoded string in a line from a
   FOO*+meltdesc.c file; the argument should point to the starting
   double-quote "; returns a malloc-ed string.  The C-encoded string
   has been produced with meltgc_add_out_cstr_len and friends like
   meltgc_add_strbuf_cstr... */
static char *
melt_c_string_in_descr (const char* p)
{
  char *res = NULL;
  struct obstack obs;
  if (!p || p[0] != '"')
    return NULL;
  memset (&obs, 0, sizeof(obs));
  obstack_init (&obs);
  p++;
  while (*p && *p != '"')
    {
      if (*p == '\\')
        {
          p++;
          switch (*p)
            {
            case 'n':
              obstack_1grow (&obs, '\n');
              p++;
              break;
            case 'r':
              obstack_1grow (&obs, '\r');
              p++;
              break;
            case 't':
              obstack_1grow (&obs, '\t');
              p++;
              break;
            case 'f':
              obstack_1grow (&obs, '\f');
              p++;
              break;
            case 'v':
              obstack_1grow (&obs, '\v');
              p++;
              break;
            case '\'':
              obstack_1grow (&obs, '\'');
              p++;
              break;
            case '"':
              obstack_1grow (&obs, '\"');
              p++;
              break;
            case '\\':
              obstack_1grow (&obs, '\\');
              p++;
              break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            {
              int c = 0;
              if (*p >= '0' && *p <= '7')
                c = c*8 + (*p - '0'), p++;
              if (*p >= '0' && *p <= '7')
                c = c*8 + (*p - '0'), p++;
              if (*p >= '0' && *p <= '7')
                c = c*8 + (*p - '0'), p++;
              obstack_1grow (&obs, (char)c);
              break;
            }
            case 'x':
            {
              char hexbuf[4] = {0,0,0,0};
              int c = 0;
              p++;
              if (ISXDIGIT(p[0])) hexbuf[0] = p[0];
              if (ISXDIGIT(p[1])) hexbuf[1] = p[1];
              p += strlen(hexbuf);
              c = strtol (hexbuf, (char**)0, 16);
              obstack_1grow (&obs, (char)c);
              break;
            }
            default:
              obstack_1grow (&obs, *p);
              p++;
              break;
            }
        }
      else
        {
          obstack_1grow (&obs, *p);
          p++;
        }
    };
  obstack_1grow (&obs, (char)0);
  res = xstrdup (XOBFINISH (&obs, char*));
  obstack_free (&obs, NULL);
  return res;
}



/* Internal function to test if a given open file has the same md5sum
   as a given hex md5string. */
static int
melt_same_md5sum_hex (const char* curpath, FILE* sfil, const char*md5hexstr)
{
  char md5tab[16];
  char md5hex[48];
  int ix = 0;
  memset (md5tab, 0, sizeof(md5tab));
  memset (md5hex, 0, sizeof(md5hex));
  if (!curpath || !sfil || !md5hexstr)
    return 0;
  if (md5_stream (sfil, &md5tab))
    melt_fatal_error ("failed to compute md5 of %s", curpath);
  for (ix = 0; ix < 16; ix++)
    {
      char hexb[4] = {0,0,0,0};
      int curbyt = md5tab[ix] & 0xff;
      snprintf (hexb, sizeof(hexb)-1, "%02x", curbyt);
      md5hex[2*ix] = hexb[0];
      md5hex[2*ix+1] = hexb[1];
    }
  return !strcmp (md5hex, md5hexstr);
}



const char* melt_flavors_array[] =
{
  "quicklybuilt",
  "optimized",
  "debugnoline",
  NULL
};

/* Return a positive index, in the melt_modulinfo, of a module
   of given source base (the path, without "+meltdesc.c" suffix of the
   MELT descriptive file). This function don't run the
   melt_start_this_module routine of the loaded module, but does dlopen
   it.  */
static int
melt_load_module_index (const char*srcbase, const char*flavor, char**errorp)
{
  int ix = -1;
  bool validh = FALSE;
  void *dlh = NULL;
  char *srcpath = NULL;
  char *sopath = NULL;
  char* sobase = NULL;
  FILE *descfil = NULL;
  char *descline = NULL;
  char* descversionmelt = NULL;
  char* descmodulename = NULL;
  char* desccumulatedhexmd5 = NULL;
  size_t descsize = 0;
  ssize_t desclinlen = 0;
  int desclinenum = 0;

  /* list of required dynamic symbols (dlsymed in the FOO module,
     provided in the FOO+meltdesc.c or FOO+melttime.h or FOO.cc
     file) */
#define MELTDESCR_REQUIRED_LIST						\
  MELTDESCR_REQUIRED_SYMBOL (melt_build_timestamp, char);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_cumulated_hexmd5, char);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_gen_timenum, long long);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_gen_timestamp, char);			\
  MELTDESCR_REQUIRED_SYMBOL (melt_lastsecfileindex, int);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_modulename, char);			\
  MELTDESCR_REQUIRED_SYMBOL (melt_prepromd5meltrun, char);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_primaryhexmd5, char);			\
  MELTDESCR_REQUIRED_SYMBOL (melt_secondaryhexmd5tab, char*);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_versionmeltstr, char);		\
  MELTDESCR_REQUIRED_SYMBOL (melt_start_this_module, melt_start_rout_t)

  /* list of optional dynamic symbols (dlsymed in the module, provided
     in the FOO+meltdesc.c or FOO+melttime.h file). */
#define MELTDESCR_OPTIONAL_LIST						\
  MELTDESCR_OPTIONAL_SYMBOL (melt_versionstr, char);			\
  MELTDESCR_OPTIONAL_SYMBOL (melt_module_is_gpl_compatible, char);	\
  MELTDESCR_OPTIONAL_SYMBOL (melt_module_nb_module_vars, int);		\
  MELTDESCR_OPTIONAL_SYMBOL (melt_modulerealpath, char);		\
  MELTDESCR_OPTIONAL_SYMBOL (melt_forwarding_module_data, melt_forwarding_rout_t); \
  MELTDESCR_OPTIONAL_SYMBOL (melt_marking_module_data, melt_marking_rout_t);


  /* declare our dynamic symbols */
#define MELTDESCR_REQUIRED_SYMBOL(Sym,Typ) Typ* dynr_##Sym = NULL
  /* Declare the required symbols */
  MELTDESCR_REQUIRED_LIST;
#undef MELTDESCR_REQUIRED_SYMBOL

#define MELTDESCR_OPTIONAL_SYMBOL(Sym,Typ) Typ* dyno_##Sym = NULL
  /* Declare the optional symbols */
  MELTDESCR_OPTIONAL_LIST;
#undef MELTDESCR_OPTIONAL_SYMBOL


#define MELTDESCR_OPTIONAL(Sym) dyno_##Sym
#define MELTDESCR_REQUIRED(Sym) dynr_##Sym

  melt_debugeprintf ("melt_load_module_index start srcbase %s flavor %s",
                     srcbase, flavor);
  if (errorp)
    *errorp = NULL;
  if (!srcbase)
    return -1;
  if (!flavor)
    flavor = MELT_DEFAULT_FLAVOR;
  if (!ISALNUM (flavor[0])
      || strchr(flavor, '.') || strchr (flavor, '/') || strchr (flavor, '+'))
    melt_fatal_error ("invalid MELT flavor %s", flavor);
  {
    std::string modbase = basename(srcbase);
    if (Melt_Module::module_of_name(modbase) != NULL)
      {
        error("MELT won't load twice a module of basename %s", modbase.c_str());
        if (errorp)
          *errorp = (char*)"duplicate MELT module";
        return -1;
      }
  }
  /* open and parse the descriptive file. */
  srcpath = concat (srcbase, MELT_DESC_FILESUFFIX, NULL);
  melt_debugeprintf ("melt_load_module_index srcpath %s flavor %s", srcpath, flavor);
  descfil = fopen (srcpath, "r");
  if (!descfil)
    {
      warning (0,
               "MELT failed to open descriptive file %s - %m", srcpath);
      goto end;
    }
  while (!feof (descfil))
    {
      char *pc = NULL;
      char *pqu1 = NULL;
      char *pqu2 = NULL;
      desclinlen = getline (&descline, &descsize, descfil);
      desclinenum ++;
      if (desclinlen>0 && descline && descline[desclinlen-1] == '\n')
        descline[--desclinlen] = (char)0;
      if (desclinlen < 0)
        break;
      /* ignore comments and short lines */
      if (desclinlen < 4) continue;
      if (descline[0] == '/' && descline[1] == '*')
        continue;
      if (descline[0] == '/' && descline[1] == '/')
        continue;
      /* ignore lines with extern "C" */
      if (strstr(descline, "extern") && strstr(descline, "\"C\""))
        continue;
      melt_debugeprintf ("melt_load_module_index #%d,len%d: %s",
                         desclinenum, (int) desclinlen, descline);
      /* parse the melt_versionmeltstr */
      if (descversionmelt == NULL
          && (pc = strstr(descline, "melt_versionmeltstr")) != NULL
          && (pqu1 = strchr (pc, '"')) != NULL
          && (pqu2 = strchr (pqu1+1, '"')) != NULL
          && pqu2 > pqu1 + 2 /* shortest version is something like 1.1 */)
        {
          melt_debugeprintf ("melt_load_module_index got versionmeltstr pc=%s pqu1=%s",
                             pc, pqu1);
          descversionmelt = melt_c_string_in_descr (pqu1);
          melt_debugeprintf ("melt_load_module_index found descversionmelt %s L%d",
                             descversionmelt, desclinenum);
        }
      /* parse the melt_modulename */
      if (descmodulename == NULL
          && (pc = strstr(descline, "melt_modulename")) != NULL
          && (pqu1 = strchr (pc, '"')) != NULL
          && (pqu2 = strchr (pqu1+1, '"')) != NULL)
        {
          descmodulename = melt_c_string_in_descr (pqu1);
          melt_debugeprintf ("melt_load_module_index found descmodulename %s L%d",
                             descmodulename, desclinenum);
        }
      /* parse the melt_cumulated_hexmd5 which should be not too short. */
      if (desccumulatedhexmd5 == NULL
          && (pc = strstr(descline, "melt_cumulated_hexmd5[]")) != NULL
          && (pqu1 = strchr (pc, '"')) != NULL
          && (pqu2 = strchr (pqu1+2, '"')) != NULL
          && pqu2 > pqu1+10 /*maybe more than 10*/)
        {
          desccumulatedhexmd5 = melt_c_string_in_descr (pqu1);
          melt_debugeprintf ("melt_load_module_index found desccumulatedhexmd5 %s L%d",
                             desccumulatedhexmd5, desclinenum);
        }
    }
  if (descfil)
    fclose (descfil), descfil= NULL;
  melt_debugeprintf ("melt_load_module_index srcpath %s after meltdescr parsing",
                     srcpath);
  /* Perform simple checks */
  if (!descmodulename)
    melt_fatal_error ("bad MELT descriptive file %s with no module name inside",
                      srcpath);
  if (!descversionmelt)
    melt_fatal_error ("bad MELT descriptive file %s with no MELT version inside",
                      srcpath);
  if (!desccumulatedhexmd5)
    melt_fatal_error ("bad MELT descriptive file %s with no cumulated hexmd5 inside",
                      srcpath);
  if (strcmp (melt_basename (descmodulename), melt_basename (srcbase)))
    warning (0,
             "MELT module name %s in MELT descriptive file %s not as expected",
             descmodulename, srcpath);
  if (!melt_flag_bootstrapping
      && strcmp(descversionmelt, melt_version_str ()))
    warning (0,
             "MELT descriptive file %s for MELT version %s, but this MELT runtime is version %s",
             srcpath, descversionmelt, melt_version_str ());
  /* Take care that the same file name should be given below, as argument to melt_compile_source.  */
  sobase =
    concat (melt_basename(descmodulename),
            ".meltmod-",
            desccumulatedhexmd5, ".", flavor,
            MELT_DYNLOADED_SUFFIX, NULL);
  melt_debugeprintf ("melt_load_module_index long sobase %s workdir %s",
                     sobase, melt_argument ("workdir"));
  if (melt_trace_module_fil)
    fprintf (melt_trace_module_fil, "base of module: %s\n", sobase);
  sopath =
    MELT_FIND_FILE
    (sobase,
     MELT_FILE_LOG, melt_trace_module_fil,
     /* First search in the temporary directory, but don't bother making it.  */
     MELT_FILE_IN_DIRECTORY, melt_tempdir,
     /* Search in the user provided work directory, if given. */
     MELT_FILE_IN_DIRECTORY, melt_argument ("workdir"),
     /* Search in the user provided module path, if given.  */
     MELT_FILE_IN_PATH, melt_argument ("module-path"),
     /* Search using the GCCMELT_MODULE_PATH environment variable.  */
     MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_MODULE_PATH",
     /* Search in the built-in MELT module directory.  */
     MELT_FILE_IN_DIRECTORY, melt_flag_bootstrapping?NULL:melt_module_dir,
     /* Since the path is a complete path with an md5um in it, we also
     search in the current directory.  */
     MELT_FILE_IN_DIRECTORY, ".",
     NULL);
  melt_debugeprintf ("melt_load_module_index sopath %s", sopath);
  /* Try also the other flavors when asked for default flavor. */
  if (!sopath && !strcmp(flavor, MELT_DEFAULT_FLAVOR) && !melt_flag_bootstrapping)
    {
      const char* curflavor = NULL;
      char* cursobase = NULL;
      char* cursopath = NULL;
      int curflavorix;
      for (curflavorix=0;
           (curflavor=melt_flavors_array[curflavorix]) != NULL;
           curflavorix++)
        {
          melt_debugeprintf ("melt_load_module_index curflavor %s curflavorix %d", curflavor, curflavorix);
          cursobase =
            concat (melt_basename(descmodulename), ".", desccumulatedhexmd5,
                    ".", curflavor, MELT_DYNLOADED_SUFFIX, NULL);
          melt_debugeprintf ("melt_load_module_index curflavor %s long cursobase %s workdir %s",
                             curflavor, cursobase, melt_argument ("workdir"));
          cursopath =
            MELT_FIND_FILE
            (cursobase,
             MELT_FILE_LOG, melt_trace_module_fil,
             /* First search in the temporary directory, but don't bother making it.  */
             MELT_FILE_IN_DIRECTORY, melt_tempdir,
             /* Search in the user provided work directory, if given. */
             MELT_FILE_IN_DIRECTORY, melt_argument ("workdir"),
             /* Search in the user provided module path, if given.  */
             MELT_FILE_IN_PATH, melt_argument ("module-path"),
             /* Search using the GCCMELT_MODULE_PATH environment variable.  */
             MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_MODULE_PATH",
             /* Search in the built-in MELT module directory.  */
             MELT_FILE_IN_DIRECTORY, melt_flag_bootstrapping?NULL:melt_module_dir,
             /* Since the path is a complete path with an md5um in it, we also
             search in the current directory.  */
             MELT_FILE_IN_DIRECTORY, ".",
             NULL);
          melt_debugeprintf ("melt_load_module_index curflavorix=%d cursopath %s", curflavorix, cursopath);
          if (cursopath)
            {
              sopath = cursopath;
              inform (UNKNOWN_LOCATION, "MELT loading module %s instead of default flavor %s",
                      cursopath, MELT_DEFAULT_FLAVOR);
              break;
            };
          free (cursobase), cursobase = NULL;
        };
    }
  /* Build the module if not found and the auto-build is not inhibited. */
  if (!sopath && !melt_flag_bootstrapping
      && !melt_argument ("inhibit-auto-build"))
    {
      const char* worktmpdir = NULL;
      const char* binbase = NULL;
      worktmpdir = melt_argument("workdir");
      if (!worktmpdir)
        worktmpdir = melt_tempdir_path (NULL, NULL);
      binbase = concat (worktmpdir, "/", melt_basename (srcbase), NULL);
      /* The same file name should be given above. */
      sopath =
        concat (binbase, ".meltmod-", desccumulatedhexmd5, ".", flavor,
                MELT_DYNLOADED_SUFFIX, NULL);
      melt_debugeprintf ("sopath %s", sopath);
      (void) remove (sopath);
      melt_compile_source (srcbase, binbase, worktmpdir, flavor);
      if (access (sopath, R_OK))
        melt_fatal_error ("inaccessible MELT module %s after auto build - %m", sopath);
    }
  if (!sopath)
    {
      /* Show various informative error messages to help the user. */
      if (sobase)
        error ("MELT failed to find module of base %s with module-path %s",
               sobase, melt_argument ("module-path"));
      if (melt_tempdir[0])
        error ("MELT failed to find module of base %s in temporary dir %s",
               srcbase, melt_tempdir);
      if (melt_argument ("workdir"))
        error ("MELT failed to find module of base %s in work dir %s",
               srcbase, melt_argument ("workdir"));
      if (melt_argument ("module-path"))
        error ("MELT failed to find module of base %s in module-path %s",
               srcbase, melt_argument ("module-path"));
      if (getenv ("GCCMELT_MODULE_PATH"))
        error ("MELT failed to find module of base %s with GCCMELT_MODULE_PATH=%s",
               srcbase, getenv ("GCCMELT_MODULE_PATH"));
      if (!melt_flag_bootstrapping)
        error ("MELT failed to find module of base %s in builtin directory %s",
               srcbase, melt_module_dir);
      if (melt_trace_module_fil)
        fflush (melt_trace_module_fil);
      else
        inform (UNKNOWN_LOCATION,
                "You could set the GCCMELT_TRACE_MODULE env.var. to some file path for debugging");
      melt_fatal_error ("No MELT module for source base %s flavor %s (parsed cumulated checksum %s)",
                        srcbase, flavor,
                        desccumulatedhexmd5 ? desccumulatedhexmd5 : "unknown");
    }
  if (!IS_ABSOLUTE_PATH (sopath))
    sopath = reconcat (sopath, getpwd (), "/", sopath, NULL);
  melt_debugeprintf ("melt_load_module_index absolute sopath %s", sopath);
  if (access (sopath, R_OK))
    melt_fatal_error ("Cannot access MELT module %s - %m", sopath);
  errno = 0;
  dlh = dlopen (sopath, RTLD_NOW | RTLD_GLOBAL);
  if (!dlh)
    {
      static char dldup[256];
      const char* dle = dlerror();
      if (!dle) dle = "??";
      strncpy(dldup, dle, sizeof(dldup)-1);
      melt_fatal_error ("Failed to dlopen MELT module %s - %s", sopath, dldup);
    }
  if (melt_trace_module_fil)
    fprintf (melt_trace_module_fil,
             "dlopened %s #%d\n", sopath, Melt_Module::nb_modules());
  validh = TRUE;

  /* Retrieve our dynamic symbols. */

#define MELTDESCR_REQUIRED_SYMBOL(Sym,Typ) do {                 \
    Typ* aptr_##Sym =                                           \
        reinterpret_cast<Typ*> (dlsym (dlh, #Sym));             \
      melt_debugeprintf ("melt_load_module_index req. " #Sym         \
                    " %p validh %d",                            \
                    (void*)aptr_##Sym, (int) validh);           \
      if (!aptr_##Sym) {                                        \
        char* dler = dlerror ();                                \
        melt_debugeprintf("melt_load_module_index req. " #Sym        \
                     " not found - %s", dler);                  \
        if (dler && errorp && !*errorp)                         \
          *errorp = concat("Cannot find " #Sym, "; ",           \
                           dler, NULL);                         \
        validh = FALSE;                                         \
      } else dynr_##Sym = aptr_##Sym; } while(0)

  /* Fetch required symbols */
  MELTDESCR_REQUIRED_LIST;

#undef MELTDESCR_REQUIRED_SYMBOL

#define MELTDESCR_OPTIONAL_SYMBOL(Sym,Typ) do {         \
    Typ* optr_##Sym					\
      = reinterpret_cast<Typ*> (dlsym (dlh, #Sym));	\
    if (optr_##Sym)					\
	dyno_##Sym = optr_##Sym; } while(0)

  /* Fetch optional symbols */
  MELTDESCR_OPTIONAL_LIST;

#undef MELTDESCR_OPTIONAL_SYMBOL

  if (!MELTDESCR_OPTIONAL(melt_module_is_gpl_compatible))
    {
      warning (0, "MELT module %s does not claim to be GPL compatible",
               MELTDESCR_REQUIRED (melt_modulename));
      inform (UNKNOWN_LOCATION,
              "see http://www.gnu.org/licenses/gcc-exception-3.1.en.html");
    }

  if (melt_flag_bootstrapping)
    {
      melt_debugeprintf ("melt_load_module_index validh %d bootstrapping melt_modulename %s descmodulename %s",
                         validh, MELTDESCR_REQUIRED (melt_modulename), descmodulename);
      validh = validh
               && !strcmp (melt_basename (MELTDESCR_REQUIRED (melt_modulename)), melt_basename (descmodulename));
    }
  else
    {
      melt_debugeprintf ("melt_load_module_index validh %d melt_modulename %s descmodulename %s",
                         validh, MELTDESCR_REQUIRED (melt_modulename), descmodulename);
      validh = validh
               && !strcmp (MELTDESCR_REQUIRED (melt_modulename), descmodulename);
    }

  melt_debugeprintf ("melt_load_module_index validh %d melt_cumulated_hexmd5 %s desccumulatedhexmd5 %s",
                     validh, MELTDESCR_REQUIRED (melt_cumulated_hexmd5), desccumulatedhexmd5);
  validh = validh
           && !strcmp (MELTDESCR_REQUIRED (melt_cumulated_hexmd5), desccumulatedhexmd5);
  melt_debugeprintf ("melt_load_module_index sopath %s validh %d melt_modulename %s melt_cumulated_hexmd5 %s",
                     sopath, (int)validh,
                     MELTDESCR_REQUIRED (melt_modulename),
                     MELTDESCR_REQUIRED (melt_cumulated_hexmd5));
  /* If the handle is still valid, perform some additional checks
     unless bootstrapping.  Issue only warnings if something is
     wrong, because intrepid users might fail these checks on
     purpose.  */
  if (validh && !melt_flag_bootstrapping)
    {
      FILE *sfil = 0;
      char *curpath = 0;
      char *srcpath = 0;
      const char* srcpathstr = melt_argument ("source-path");
      int nbsecfile = 0;
      int cursecix = 0;
      time_t gentim = 0;
      time_t nowt = 0;
      time (&nowt);
      if (strcmp (MELTDESCR_REQUIRED (melt_versionmeltstr),
                  melt_version_str ()))
        warning (0,
                 "MELT module %s for source %s has mismatching MELT version %s, expecting %s",
                 sopath, srcbase, MELTDESCR_REQUIRED (melt_versionmeltstr), melt_version_str ());
      if (strcmp (MELTDESCR_REQUIRED (melt_prepromd5meltrun),
                  melt_run_preprocessed_md5))
        warning (0,
                 "MELT module %s for source %s has mismatching melt-run.h signature %s, expecting %s",
                 sopath, srcbase, MELTDESCR_REQUIRED (melt_prepromd5meltrun),
                 melt_run_preprocessed_md5);
      nbsecfile = *(MELTDESCR_REQUIRED(melt_lastsecfileindex));
      melt_debugeprintf ("melt_load_module_index descmodulename %s nbsecfile %d", descmodulename, nbsecfile);
      srcpath = concat (descmodulename, ".cc", NULL);
      melt_debugeprintf ("melt_load_module_index srcpath=%s", srcpath);
      curpath =
        MELT_FIND_FILE (srcpath,
                        MELT_FILE_LOG, melt_trace_source_fil,
                        MELT_FILE_IN_DIRECTORY, ".",
                        MELT_FILE_IN_PATH, srcpathstr,
                        MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_SOURCE_PATH",
                        MELT_FILE_IN_DIRECTORY, melt_source_dir,
                        /* also search in the temporary directory, but don't bother making it.  */
                        MELT_FILE_IN_DIRECTORY, melt_tempdir,
                        /* Search in the user provided work directory, if given. */
                        MELT_FILE_IN_DIRECTORY, melt_argument ("workdir"),
                        NULL);
      melt_debugeprintf ("melt_load_module_index srcpath %s ", srcpath);
      melt_debugeprintf ("melt_load_module_index curpath %s ", curpath);
      if (!curpath)
        warning (0,
                 "MELT module %s cannot find its source path for base %s flavor %s",
                 sopath, srcbase, flavor);
      else
        {
          sfil = fopen (curpath, "r");
          if (!sfil)
            warning (0,
                     "MELT module %s cannot open primary source file %s for %s - %m", sopath, curpath, srcbase);
          else
            {
              if (!melt_same_md5sum_hex (curpath, sfil, MELTDESCR_REQUIRED (melt_primaryhexmd5)))
                warning (0,
                         "MELT primary source file %s has mismatching md5sum, expecting %s",
                         curpath, MELTDESCR_REQUIRED (melt_primaryhexmd5));
              fclose (sfil), sfil = NULL;
            };
        }
      free (srcpath), srcpath = NULL;
      free (curpath), curpath = NULL;
      for (cursecix = 1; cursecix < nbsecfile; cursecix++)
        {
          char suffixbuf[32];
          if (MELTDESCR_REQUIRED(melt_secondaryhexmd5tab)[cursecix] == NULL)
            continue;
          memset (suffixbuf, 0, sizeof(suffixbuf));
          snprintf (suffixbuf, sizeof(suffixbuf)-1, "+%02d.cc", cursecix);
          srcpath = concat (descmodulename, suffixbuf, NULL);
          curpath =
            MELT_FIND_FILE (srcpath,
                            MELT_FILE_LOG, melt_trace_source_fil,
                            MELT_FILE_IN_DIRECTORY, ".",
                            MELT_FILE_IN_PATH, srcpathstr,
                            MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_SOURCE_PATH",
                            MELT_FILE_IN_DIRECTORY, melt_source_dir,
                            NULL);
          melt_debugeprintf ("melt_load_module_index srcpath %s ", srcpath);
          sfil = fopen (curpath, "r");
          if (!sfil)
            warning (0,
                     "MELT module %s cannot open secondary source file %s - %m",
                     sopath, curpath);
          else
            {
              if (!melt_same_md5sum_hex (curpath, sfil,
                                         MELTDESCR_REQUIRED(melt_secondaryhexmd5tab)[cursecix]))
                warning (0,
                         "MELT secondary source file %s has mismatching md5sum, expecting %s",
                         curpath, MELTDESCR_REQUIRED(melt_secondaryhexmd5tab)[cursecix]);
              fclose (sfil), sfil = NULL;
            };
          free (srcpath), srcpath = NULL;
          free (curpath), curpath = NULL;
        };
      if (MELTDESCR_OPTIONAL(melt_versionstr)
          && strcmp(MELTDESCR_OPTIONAL(melt_versionstr), melt_version_str()))
        warning (0,
                 "MELT module %s generated by %s but used by %s [possible version mismatch]",
                 sopath, MELTDESCR_OPTIONAL(melt_versionstr), melt_version_str ());
      gentim = (time_t) (*MELTDESCR_REQUIRED(melt_gen_timenum));
      if (gentim > nowt)
        warning (0,
                 "MELT module %s apparently generated in the future %s, now is %s",
                 sopath, MELTDESCR_REQUIRED(melt_gen_timestamp), ctime (&nowt));
    };
  melt_debugeprintf ("melt_load_module_index sopath %s validh %d dlh %p",
                     sopath, (int)validh, dlh);
  if (validh)
    {
      melt_debugeprintf ("melt_load_module_index making Melt_Plain_Module of sopath=%s srcbase=%s dlh=%p",
                         sopath, srcbase, dlh);
      Melt_Plain_Module* pmod = new Melt_Plain_Module(sopath, srcbase, dlh);
      gcc_assert (pmod->valid_magic());
      pmod->set_forwarding_routine (MELTDESCR_OPTIONAL (melt_forwarding_module_data));
      pmod->set_marking_routine (MELTDESCR_OPTIONAL (melt_marking_module_data));
      pmod->set_start_routine (MELTDESCR_REQUIRED (melt_start_this_module));
      ix = pmod->index ();
      melt_debugeprintf ("melt_load_module_index successful ix %d srcbase %s sopath %s flavor %s",
                         ix, srcbase, sopath, flavor);
      if (!quiet_flag || melt_flag_debug)
        {
          if (MELTDESCR_OPTIONAL(melt_modulerealpath))
            inform (UNKNOWN_LOCATION,
                    "MELT loading module #%d for %s [realpath %s] with %s generated at %s built %s",
                    ix, srcbase, MELTDESCR_OPTIONAL(melt_modulerealpath), sopath,
                    MELTDESCR_REQUIRED(melt_gen_timestamp),
                    MELTDESCR_REQUIRED(melt_build_timestamp));
        }
    }
  else
    {
      melt_debugeprintf ("melt_load_module_index invalid dlh %p sopath %s", dlh, sopath);
      dlclose (dlh), dlh = NULL;
    }
end:
  if (srcpath)
    free (srcpath), srcpath= NULL;
  if (descfil)
    fclose (descfil), descfil= NULL;
  if (descline)
    free (descline), descline = NULL;
  if (descversionmelt)
    free (descversionmelt), descversionmelt = NULL;
  if (desccumulatedhexmd5)
    free (desccumulatedhexmd5), desccumulatedhexmd5 = NULL;
  if (sopath)
    free (sopath), sopath = NULL;
  if (sobase)
    free (sobase), sobase = NULL;
  melt_debugeprintf ("melt_load_module_index srcbase %s flavor %s return ix %d",
                     srcbase, flavor, ix);
  return ix;
}




melt_ptr_t
meltgc_run_cc_extension (melt_ptr_t basename_p, melt_ptr_t env_p, melt_ptr_t litvaltup_p)
{
  /* list of required dynamic symbols (dlsymed in the FOO module,
     provided in the FOO+meltdesc.c or FOO+melttime.h or FOO.c
     file) */
#define MELTRUNDESCR_REQUIRED_LIST					\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_build_timestamp, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_cumulated_hexmd5, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_gen_timenum, long long);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_gen_timestamp, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_lastsecfileindex, int);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_modulename, char);			\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_prepromd5meltrun, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_primaryhexmd5, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_secondaryhexmd5tab, char*);	\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_versionmeltstr, char);		\
  MELTRUNDESCR_REQUIRED_SYMBOL (melt_start_run_extension, melt_start_runext_rout_t)

  /* list of optional dynamic symbols (dlsymed in the module, provided
     in the FOO+meltdesc.c or FOO+melttime.h file). */
#define MELTRUNDESCR_OPTIONAL_LIST					\
  MELTRUNDESCR_OPTIONAL_SYMBOL (melt_versionstr, char);			\
  MELTRUNDESCR_OPTIONAL_SYMBOL (melt_forwarding_module_data, melt_forwarding_rout_t); \
  MELTRUNDESCR_OPTIONAL_SYMBOL (melt_marking_module_data, melt_marking_rout_t);

  /* declare our dynamic symbols */
#define MELTRUNDESCR_REQUIRED_SYMBOL(Sym,Typ) Typ* dynr_##Sym = NULL
  MELTRUNDESCR_REQUIRED_LIST;
#undef MELTRUNDESCR_REQUIRED_SYMBOL
#define MELTRUNDESCR_OPTIONAL_SYMBOL(Sym,Typ) Typ* dyno_##Sym = NULL
  MELTRUNDESCR_OPTIONAL_LIST;
#undef MELTRUNDESCR_OPTIONAL_SYMBOL
#define MELTRUNDESCR_OPTIONAL(Sym) dyno_##Sym
#define MELTRUNDESCR_REQUIRED(Sym) dynr_##Sym
  char basenamebuf[128];
  char* descversionmelt = NULL;
  char* descpath = NULL;
  FILE *descfile = NULL;
  char *descline = NULL;
  size_t descsize = 0;
  ssize_t desclinlen = 0;
  int desclinenum = 0;
  int nbsecfileindex = -1;
  char* sopath = NULL;
  void* dlh = NULL;
  char descmd5hex[36];		/* 32 would be enough, but we want a
				   zero terminated string. */
  MELT_ENTERFRAME (5, NULL);
#define resv          meltfram__.mcfr_varptr[0]
#define basenamev     meltfram__.mcfr_varptr[1]
#define environv      meltfram__.mcfr_varptr[2]
#define litvaltupv    meltfram__.mcfr_varptr[3]
#define envrefv       meltfram__.mcfr_varptr[4]
  basenamev = basename_p;
  environv = env_p;
  litvaltupv = litvaltup_p;
  memset (descmd5hex, 0, sizeof(descmd5hex));
  if (!basenamev || !environv || !litvaltupv)
    goto end;
  {
    const char* basestr = melt_string_str ((melt_ptr_t) basenamev);
    if (!basestr)
      goto end;
    memset (basenamebuf, 0, sizeof(basenamebuf));
    strncpy (basenamebuf, basestr, sizeof(basenamebuf)-1);
    if (strcmp(basestr, basenamebuf))
      {
        /* This probably should never happen, unless the basenamev is
           a too long name. */
        melt_fatal_error
        ("MELT runnning extension buffered basename %s different of %s",
         basenamebuf, basestr);
        goto end;
      }
  }
  melt_debugeprintf ("meltgc_run_cc_extension basenamebuf=%s", basenamebuf);
  descpath = melt_tempdir_path (basenamebuf, "+meltdesc.c");
  melt_debugeprintf ("meltgc_run_cc_extension descpath=%s", descpath);
  descfile = fopen (descpath, "r");
  if (!descfile)
    {
      warning (0,
               "MELT running extension descriptor file %s not found - %s",
               descpath, xstrerror (errno));
      goto end;
    }
  while (!feof (descfile))
    {
      char *pc = NULL;
      char *pqu1 = NULL;
      char *pqu2 = NULL;
      desclinlen = getline (&descline, &descsize, descfile);
      desclinenum ++;
      if (desclinlen>0 && descline && descline[desclinlen-1] == '\n')
        descline[--desclinlen] = (char)0;
      if (desclinlen < 0)
        break;
      /* ignore comments and short lines */
      if (desclinlen < 4) continue;
      if (descline[0] == '/' && descline[1] == '*')
        continue;
      if (descline[0] == '/' && descline[1] == '/')
        continue;
      /* ignore lines with extern "C" */
      if (strstr(descline, "extern") && strstr(descline, "\"C\""))
        continue;
      /* parse the melt_versionmeltstr */
      if (descversionmelt == NULL
          && (pc = strstr(descline, "melt_versionmeltstr[]")) != NULL
          && (pqu1 = strchr (pc, '"')) != NULL
          && (pqu2 = strchr (pqu1+1, '"')) != NULL
          && pqu2 > pqu1 + 10 /*actually should be more than 10*/)
        {
          descversionmelt = melt_c_string_in_descr (pqu1);
          melt_debugeprintf ("meltgc_run_cc_extension found descversionmelt %s", descversionmelt);
        };
      /* check that melt_lastsecfileindex is 0 */
      if (nbsecfileindex < 0 && (pc = strstr(descline, "melt_lastsecfileindex"))!= NULL
          && (pqu1 = strchr(pc, '=')) != NULL)
        if ((nbsecfileindex = atoi (pqu1+1))>0)
          melt_fatal_error ("cannot handle multi-C-file runtime extension %s [%d]",
                            basenamebuf, nbsecfileindex);
      /* parse the melt_primaryhexmd5 */
      if (descmd5hex[0] == (char)0
          && (pc = strstr(descline, "melt_primaryhexmd5[]")) != NULL
          && (pqu1 = strchr (pc, '\"')) != NULL
          && (pqu2 = strchr (pqu1+1, '\"')) != NULL
          && (pqu2 >= pqu1 + 20) /* actually 32 */
          && (pqu2 < pqu1 + sizeof(descmd5hex)))
        strncpy (descmd5hex, pqu1+1, pqu2-pqu1-1);
    };				/* end loop reading descfile */
  /* check that the md5sum of the primary C file is descmd5hex */
  {
    char compmd5buf[40];	/* Should be bigger than 32, for the
				   terminating null char. */
    char* cpath = NULL;
    memset (compmd5buf, 0, sizeof(compmd5buf));
    cpath = concat (basenamebuf, ".cc", NULL);
    if (access (cpath, R_OK))
      melt_fatal_error ("cannot access runtime extension primary C++ file %s - %s",
                        cpath, xstrerror (errno));
    melt_string_hex_md5sum_file_to_hexbuf (cpath, compmd5buf);
    if (strcmp (compmd5buf, descmd5hex))
      melt_fatal_error ("runtime extension primary file %s has md5sum of %s but expecting %s",
                        cpath, compmd5buf, descmd5hex);
    free (cpath), cpath = NULL;
  }
  sopath = concat (basenamebuf,
                   ".meltmod-",
                   descmd5hex,
                   ".runextend",
                   MELT_DYNLOADED_SUFFIX,
                   NULL);
  melt_debugeprintf ("meltgc_run_cc_extension sopath=%s", sopath);
  if (access (sopath, R_OK))
    melt_fatal_error ("runtime extension module %s not accessible - %s",
                      sopath, xstrerror(errno));
  melt_debugeprintf("meltgc_run_cc_extension sopath %s before dlopen", sopath);
  dlh = dlopen (sopath, RTLD_NOW | RTLD_GLOBAL);
  if (!dlh)
    {
      static char dldup[256];
      const char*dle = dlerror();
      if (!dle) dle = "??";
      strncpy(dldup, dle, sizeof(dldup)-1);
      melt_fatal_error ("failed to dlopen runtime extension %s - %s",
                        sopath, dldup);
    }
  MELT_LOCATION_HERE ("meltgc_run_cc_extension after dlopen");


  /* load the required and optional symbols */
#define MELTRUNDESCR_REQUIRED_SYMBOL(Sym,Typ) do   {	\
  dynr_##Sym = (Typ*) dlsym (dlh, #Sym);		\
      if (!dynr_##Sym)					\
	melt_fatal_error				\
	  ("failed to get " #Sym			\
	   " from runtime extension %s - %s",		\
	   sopath, dlerror());				\
  } while(0)
  MELTRUNDESCR_REQUIRED_LIST;
#undef MELTRUNDESCR_REQUIRED_SYMBOL
#define MELTRUNDESCR_OPTIONAL_SYMBOL(Sym,Typ) \
  dyno_##Sym = reinterpret_cast<Typ*> (dlsym (dlh, #Sym));
  MELTRUNDESCR_OPTIONAL_LIST;
#undef MELTRUNDESCR_OPTIONAL_SYMBOL
  /* check the primary md5sum */
  if (!dynr_melt_primaryhexmd5
      || strcmp(dynr_melt_primaryhexmd5, descmd5hex))
    melt_fatal_error ("invalid primary md5sum in runtime extension %s - got %s expecting %s",
                      sopath, dynr_melt_primaryhexmd5, descmd5hex);
  {
    int ix = 0;
    /* check the melt_versionstr of the extension */
    if (dyno_melt_versionstr)
      {
        if (strcmp(dyno_melt_versionstr, melt_version_str()))

          melt_fatal_error ("runtime extension %s for MELT version %s but this MELT expects %s",
                            basenamebuf, dyno_melt_versionstr, melt_version_str());
      }
    {
      Melt_Extension_Module* xmod = new Melt_Extension_Module(sopath, basenamebuf, dlh);
      gcc_assert (xmod->valid_magic());
      ix = xmod->index ();
      gcc_assert (ix>0);
      xmod->set_forwarding_routine (MELTDESCR_OPTIONAL (melt_forwarding_module_data));
      xmod->set_marking_routine (MELTDESCR_OPTIONAL (melt_marking_module_data));
      melt_debugeprintf ("meltgc_run_cc_extension %s has index %d",
                         basenamebuf, ix);
    }
  }
  envrefv = meltgc_new_reference ((melt_ptr_t) environv);
  melt_debugeprintf ("meltgc_run_cc_extension envrefv@%p", (void*)envrefv);
  {
#if MELT_HAVE_RUNTIME_DEBUG>0
    char locbuf[96];
    memset (locbuf,0,sizeof(locbuf));
    MELT_LOCATION_HERE_PRINTF (locbuf,
                               "run-cc-extension %s", melt_basename (basenamebuf));
#endif
    melt_debugeprintf ("meltgc_run_cc_extension before calling dynr_melt_start_run_extension@%p",
                       (void*) dynr_melt_start_run_extension);
    resv = (*dynr_melt_start_run_extension) ((melt_ptr_t)envrefv,
           (melt_ptr_t)litvaltupv);
    melt_debugeprintf ("meltgc_run_cc_extension after call resv=%p", (void*)resv);
    MELT_LOCATION_HERE ("meltgc_run_cc_extension ending");
  }
end:
  if (descpath)
    free (descpath), descpath = NULL;
  if (sopath)
    free (sopath), sopath = NULL;
  if (descfile)
    fclose (descfile), descfile = NULL;
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef MELTRUNDESCR_REQUIRED_LIST
#undef MELTRUNDESCR_OPTIONAL_LIST
#undef MELTRUNDESCR_OPTIONAL
#undef MELTRUNDESCR_REQUIRED
#undef basenamev
#undef environv
#undef resv
#undef envrefv
}




melt_ptr_t
meltgc_start_module_by_index (melt_ptr_t env_p, int modix)
{
  char locbuf[200];
  memset (locbuf, 0, sizeof(locbuf));
  MELT_ENTERFRAME(2, NULL);
#define resmodv   meltfram__.mcfr_varptr[0]
#define env       meltfram__.mcfr_varptr[1]
  env = env_p;
  Melt_Module* cmod = Melt_Module::nth_module (modix);
  if (!cmod)
    {
      warning (0, "invalid MELT module index #%d to start", modix);
      goto end;
    };
  gcc_assert (cmod->valid_magic ());
  {
    Melt_Plain_Module* plmod = cmod->as_plain_module();
    if (!plmod)
      {
        warning (0, "MELT module %s index #%d is not plain, so cannot be started",
                 cmod->module_path (), modix);
        goto end;
      }
    MELT_LOCATION_HERE_PRINTF (locbuf,
                               "meltgc_start_module_by_index before starting #%d %s",
                               modix, plmod->module_path());
    resmodv = plmod->start_it(env);
    if (!resmodv)
      {
        warning (0, "MELT module %s index #%d was not started",  plmod->module_path(),  modix);
      }
    MELT_LOCATION_HERE_PRINTF
    (locbuf,
     "meltgc_start_module_by_index after starting #%d", modix);
  }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resmodv;
#undef resmodv
#undef env
}

melt_ptr_t
meltgc_start_all_new_modules (melt_ptr_t env_p)
{
  char locbuf[200];
  MELT_ENTERFRAME(1, NULL);
#define env       meltfram__.mcfr_varptr[0]
  env = env_p;
  melt_debugeprintf ("meltgc_start_all_new_modules env %p", (void*) env);
  for (int modix = 1;
       modix <= Melt_Module::nb_modules();
       modix++)
    {
      Melt_Module* cmod = Melt_Module::nth_module(modix);
      if (!cmod)
        continue;
      gcc_assert (cmod->valid_magic());
      Melt_Plain_Module* plmod = cmod->as_plain_module();
      if (!plmod)
        continue;
      if (plmod->started()) continue;
      unsigned nbkw = 2+ melt_minorsizekilow/3;
      MELT_LOCATION_HERE_PRINTF
      (locbuf, "meltgc_start_all_new_modules before reserving %d Kw for #%d module %s",
       nbkw, modix, plmod->module_path());
      meltgc_reserve(nbkw*1024*sizeof(void*));
      MELT_LOCATION_HERE_PRINTF
      (locbuf, "meltgc_start_all_new_modules before starting #%d module %s",
       modix, plmod->module_path());
      melt_debugeprintf ("meltgc_start_all_new_modules env %p before starting modix %d",
                         (void*) env, modix);
      env = meltgc_start_module_by_index ((melt_ptr_t) env, modix);
      if (!env)
        melt_fatal_error ("MELT failed to start module #%d %s",
                          modix, plmod->module_path());
    }
  MELT_EXITFRAME ();
  return (melt_ptr_t) env;
#undef env
}


#define MODLIS_SUFFIX ".modlis"
#define MODLIS_MAXDEPTH 8



/* Load a single module, but don't initialize it. */
int
meltgc_load_flavored_module (const char*modulbase, const char*flavor)
{
  const char* srcpathstr = melt_argument ("source-path");
  char* dupmodul = NULL;
  char* descrpath = NULL;
  char* descrfull = NULL;
  char* tempdirpath = melt_tempdir_path(NULL, NULL);
  int modix = 0;
  /* The location buffer is local, since this function may recurse!  */
  char curlocbuf[160];
  MELT_ENTEREMPTYFRAME (NULL);
  memset (curlocbuf, 0, sizeof (curlocbuf));
  melt_debugeprintf("meltgc_load_flavored_module start base %s flavor %s tempdirpath %s",
                    modulbase, flavor, tempdirpath);
  if (!modulbase || !modulbase[0])
    goto end;
  dupmodul = xstrdup(modulbase);
  if (!flavor || !flavor[0])
    flavor = MELT_DEFAULT_FLAVOR;
  melt_debugeprintf ("meltgc_load_flavored_module dupmodul %s flavor %s",
                     dupmodul, flavor);
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "meltgc_load_flavored_module module %s flavor %s",
                             dupmodul, flavor);
  {
    const char *modumelt_basename = melt_basename (modulbase);
    if (modumelt_basename && strchr (modumelt_basename, '.'))
      melt_fatal_error ("invalid module base to load %s with dot in base name",
                        modulbase);
  }
  descrfull = concat (dupmodul, MELT_DESC_FILESUFFIX, NULL);
  melt_debugeprintf ("meltgc_load_flavored_module descrfull %s flavor %s",
                     descrfull, flavor);
  descrpath =
    MELT_FIND_FILE (descrfull,
                    MELT_FILE_LOG, melt_trace_source_fil,
                    MELT_FILE_IN_DIRECTORY, tempdirpath,
                    MELT_FILE_IN_DIRECTORY, ".",
                    MELT_FILE_IN_PATH, srcpathstr,
                    MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_SOURCE_PATH",
                    MELT_FILE_IN_DIRECTORY, melt_flag_bootstrapping?NULL:melt_source_dir,
                    NULL);
  melt_debugeprintf ("meltgc_load_flavored_module descrpath %s dupmodul %s",
                     descrpath, dupmodul);
  if (!descrpath)
    {
      error ("MELT failed to find module %s with descriptive file %s",
             dupmodul, descrfull);
      /* Keep the order of the inform calls below same as the order
      for MELT_FIND_FILE above. */
      if (tempdirpath && tempdirpath[0])
        inform (UNKNOWN_LOCATION,
                "MELT temporary directory %s", tempdirpath);
      inform (UNKNOWN_LOCATION,
              "MELT current directory %s", getpwd());
      if (srcpathstr)
        inform (UNKNOWN_LOCATION,
                "MELT source path %s", srcpathstr);
      if (getenv ("GCCMELT_SOURCE_PATH"))
        inform (UNKNOWN_LOCATION,
                "GCCMELT_SOURCE_PATH from environment %s",
                getenv ("GCCMELT_SOURCE_PATH"));
      if (!melt_flag_bootstrapping)
        inform (UNKNOWN_LOCATION,
                "builtin MELT source directory %s", melt_source_dir);
      melt_fatal_error ("failed to find MELT module %s", dupmodul);
    }
  if (!IS_ABSOLUTE_PATH(descrpath))
    {
      char *realdescrpath = lrealpath (descrpath);
      melt_debugeprintf ("meltgc_load_flavored_module realdescrpath %s",
                         realdescrpath);
      free (descrpath), descrpath = NULL;
      gcc_assert (realdescrpath != NULL);
      descrpath = realdescrpath;
    }
  /* remove the +meltdesc.c suffix */
  {
    char* pc = strstr (descrpath, MELT_DESC_FILESUFFIX);
    gcc_assert (pc != NULL);
    *pc = (char)0;
  }
  melt_debugeprintf ("meltgc_load_flavored_module truncated descrpath %s flavor %s before melt_load_module_index",
                     descrpath, flavor);
  {
    char *moderr = NULL;
    modix = melt_load_module_index (descrpath, flavor, &moderr);
    melt_debugeprintf ("meltgc_load_flavored_module after melt_load_module_index modix %d descrpath %s",
                       modix, descrpath);
    if (modix < 0)
      melt_fatal_error ("failed to load MELT module %s flavor %s - %s",
                        descrpath, flavor, moderr?moderr:"...");
  }
end:
  MELT_EXITFRAME ();
  if (descrpath)
    free (descrpath), descrpath = NULL;
  if (tempdirpath)
    free (tempdirpath), tempdirpath = NULL;
  melt_debugeprintf ("meltgc_load_flavored_module modul %s return modix %d",
                     dupmodul, modix);
  if (dupmodul)
    free (dupmodul), dupmodul = NULL;
  return modix;
}


melt_ptr_t
meltgc_start_flavored_module (melt_ptr_t env_p, const char*modulbase, const char*flavor)
{
  char *moduldup = NULL;
  char *flavordup = NULL;
  int modix = -1;
  char modulbuf[80];
  char flavorbuf[32];
  /* The location buffer is local, since this function may recurse!  */
  char curlocbuf[200];
  MELT_ENTERFRAME(1, NULL);
#define env       meltfram__.mcfr_varptr[0]
  memset (curlocbuf, 0, sizeof (curlocbuf));
  env = env_p;
  memset (modulbuf, 0, sizeof(modulbuf));
  memset (flavorbuf, 0, sizeof(flavorbuf));
  melt_debugeprintf ("meltgc_start_flavored_module env %p modulbase %s flavor %s",
                     (void*) env, modulbase?modulbase:"*none*", flavor?flavor:"*none*");
  if (!modulbase)
    {
      env = NULL;
      goto end;
    }
  /* copy the flavor and the modulebase */
  if (strlen (modulbase) < sizeof(modulbuf))
    {
      strncpy (modulbuf, modulbase, sizeof(modulbuf));
      moduldup = modulbuf;
    }
  else
    moduldup = xstrdup (modulbase);
  if (!flavor)
    flavordup = NULL;
  else if (strlen (flavor) < sizeof(flavorbuf))
    {
      strncpy (flavorbuf, flavor, sizeof(flavorbuf));
      flavordup = flavorbuf;
    }
  else
    flavordup = xstrdup (flavor);
  if (flavordup)
    {
      char *pc;
      for (pc = flavordup; *pc; pc++)
        *pc = TOLOWER (*pc);
    }
  melt_debugeprintf ("meltgc_start_flavored_module moduldup %s flavordup %s before load",
                     moduldup?moduldup:"*none*", flavordup?flavordup:"*none*");
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "meltgc_start_flavored_module module %s flavor %s",
                             moduldup, flavordup?flavordup:"*none*");
  modix = meltgc_load_flavored_module (moduldup, flavordup);
  melt_debugeprintf ("meltgc_start_flavored_module moduldup %s flavordup %s got modix %d",
                     moduldup, flavordup?flavordup:"*none*", modix);
  if (modix < 0)
    {
      error ("MELT failed to load started module %s flavor %s",
             moduldup, flavordup?flavordup:"*none*");
      env = NULL;
      goto end;
    }
  melt_debugeprintf ("meltgc_start_flavored_module moduldup %s before starting all new", moduldup);
  env = meltgc_start_all_new_modules ((melt_ptr_t) env);
  melt_debugeprintf ("meltgc_start_flavored_module moduldup %s after starting all new env %p", moduldup, (void*) env);
end:
  if (moduldup && moduldup != modulbuf)
    free (moduldup), moduldup = NULL;
  if (flavordup && flavordup != flavorbuf)
    free (flavordup), flavordup = NULL;
  MELT_EXITFRAME ();
  return (melt_ptr_t) env;
#undef env
}


int
meltgc_load_one_module (const char*flavoredmodule)
{
  int modix = -1;
  char tinybuf[80];
  char* dupflavmod = NULL;
  char* dotptr = NULL;
  char* flavor = NULL;
  /* The location buffer is local, since this function may recurse!  */
  char curlocbuf[200];
  memset (curlocbuf, 0, sizeof (curlocbuf));
  MELT_ENTEREMPTYFRAME (NULL);
  if (!flavoredmodule)
    goto end;
  memset (tinybuf, 0, sizeof(tinybuf));
  melt_debugeprintf ("meltgc_load_one_module start flavoredmodule %s",
                     flavoredmodule);
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "meltgc_load_one_module flavoredmodule %s",
                             flavoredmodule);
  if (strlen (flavoredmodule) < sizeof(tinybuf)-1)
    {
      strncpy (tinybuf, flavoredmodule, sizeof(tinybuf)-1);
      dupflavmod = tinybuf;
    }
  else
    dupflavmod = xstrdup (flavoredmodule);
  dotptr = CONST_CAST (char*, strchr (melt_basename (dupflavmod), '.'));
  if (dotptr)
    {
      *dotptr = (char)0;
      flavor = dotptr + 1;
      melt_debugeprintf ("meltgc_load_one_module got flavor %s", flavor);
    }
  melt_debugeprintf ("meltgc_load_one_module before loading module %s flavor %s",
                     dupflavmod, flavor?flavor:"*none*");
  modix = meltgc_load_flavored_module (dupflavmod, flavor);
  melt_debugeprintf ("meltgc_load_one_module after loading module %s modix %d",
                     dupflavmod, modix);
end:
  if (dupflavmod && dupflavmod != tinybuf)
    free (dupflavmod), dupflavmod = NULL;
  melt_debugeprintf ("meltgc_load_one_module flavoredmodule %s gives modix %d",
                     flavoredmodule, modix);
  MELT_EXITFRAME ();
  return modix;
}



/* Load a module list, but don't initialize the modules yet. */
void
meltgc_load_module_list (int depth, const char *modlistbase)
{
  FILE *filmod = NULL;
  char *modlistfull = NULL;
  char *modlistpath = NULL;
  char *modlin = NULL;
  size_t modlinsiz = 0;
  ssize_t modlinlen = 0;
  int modlistbaselen = 0;
  int lincnt = 0;
  const char* srcpathstr = melt_argument ("source-path");
  /* The location buffer is local, since this function recurses!  */
  char curlocbuf[200];
  memset (curlocbuf, 0, sizeof (curlocbuf));
  MELT_ENTEREMPTYFRAME (NULL);
  melt_debugeprintf("meltgc_load_module_list start modlistbase %s depth %d",
                    modlistbase, depth);
  MELT_LOCATION_HERE_PRINTF (curlocbuf,
                             "meltgc_load_module_list start depth %d modlistbase %s",
                             depth, modlistbase);
  if (!modlistbase)
    goto end;
  if (melt_trace_source_fil)
    {
      fprintf (melt_trace_source_fil, "Loading module list %s at depth %d\n", modlistbase, depth);
      fflush (melt_trace_source_fil);
    };
  modlistbaselen = strlen (modlistbase);
  if (modlistbaselen > (int) strlen (MODLIS_SUFFIX)
      && !strcmp(modlistbase + modlistbaselen - strlen(MODLIS_SUFFIX), MODLIS_SUFFIX))
    melt_fatal_error ("MELT module list %s should not be given with its suffix %s",
                      modlistbase, MODLIS_SUFFIX);
  modlistfull = concat (modlistbase, MODLIS_SUFFIX, NULL);
  modlistpath =
    MELT_FIND_FILE (modlistfull,
                    MELT_FILE_LOG, melt_trace_source_fil,
                    MELT_FILE_IN_DIRECTORY, ".",
                    MELT_FILE_IN_PATH, srcpathstr,
                    MELT_FILE_IN_ENVIRON_PATH, melt_flag_bootstrapping?NULL:"GCCMELT_SOURCE_PATH",
                    MELT_FILE_IN_DIRECTORY, melt_flag_bootstrapping?NULL:melt_source_dir,
                    NULL);
  melt_debugeprintf ("meltgc_load_module_list modlistpath %s", modlistpath);
  if (!modlistpath)
    {
      error ("cannot load MELT module list %s", modlistbase);
      if (srcpathstr)
        inform (UNKNOWN_LOCATION,
                "MELT source path %s", srcpathstr);
      if (getenv ("GCCMELT_SOURCE_PATH"))
        inform (UNKNOWN_LOCATION,
                "GCCMELT_SOURCE_PATH from environment %s",
                getenv ("GCCMELT_SOURCE_PATH"));
      if (!melt_flag_bootstrapping)
        inform (UNKNOWN_LOCATION,
                "builtin MELT source directory %s", melt_source_dir);
      if (melt_trace_source_fil)
        fflush (melt_trace_source_fil);
      else
        inform (UNKNOWN_LOCATION,
                "You could set GCCMELT_TRACE_SOURCE env.var. to a file path for tracing module list loads");
      melt_fatal_error ("MELT failed to load module list %s", modlistfull);
    }
  if (!IS_ABSOLUTE_PATH (modlistpath))
    {
      char *realmodlistpath = lrealpath (modlistpath);
      melt_debugeprintf ("real module list path %s", realmodlistpath);
      free (modlistpath), modlistpath = NULL;
      modlistpath = realmodlistpath;
    }
  filmod = fopen (modlistpath, "r");
  melt_debugeprintf ("reading module list '%s'", modlistpath);
  if (!filmod)
    melt_fatal_error ("failed to open melt module list file %s - %m",
                      modlistpath);
  while (!feof (filmod))
    {
      modlinlen = getline (&modlin, &modlinsiz, filmod);
      lincnt++;
      if (modlinlen <= 0 || modlin[0] == '#' || modlin[0] == '\n')
        continue;
      while (modlinlen > 0 && ISSPACE(modlin[modlinlen-1]))
        modlin[--modlinlen] = (char)0;
      melt_debugeprintf ("meltgc_load_module_list line #%d: %s", lincnt, modlin);
      MELT_LOCATION_HERE_PRINTF
      (curlocbuf,
       "meltgc_load_module_list %s line %d: %s",
       modlistpath, lincnt, modlin);
      /* Handle nested module lists */
      if (modlin[0] == '@')
        {
          if (depth > MODLIS_MAXDEPTH)
            melt_fatal_error ("MELT has too nested [%d] module list %s with %s",
                              depth, modlistbase, modlin);
          MELT_LOCATION_HERE_PRINTF
          (curlocbuf,
           "meltgc_load_module_list %s recursive line %d: '%s'",
           modlistpath, lincnt, modlin);
          melt_debugeprintf ("meltgc_load_module_list recurse depth %d sublist '%s'", depth, modlin+1);
          meltgc_load_module_list (depth+1, modlin+1);
        }
      /* Handle mode-conditional module item */
      else if (modlin[0] == '?')
        {
          std::string condmodstr;
          std::string condcompstr;
          for (unsigned ix=1; modlin[ix] && (ISALNUM(modlin[ix])||modlin[ix]=='_'); ix++)
            condmodstr += modlin[ix];
          {
            unsigned cix = 1+condmodstr.size();
            while (cix<modlinlen && ISSPACE(modlin[cix])) cix++;
            condcompstr = modlin+cix;
          }
          melt_debugeprintf ("meltgc_load_module_list modeconditional condmod='%s' condcomp='%s'",
                             condmodstr.c_str(), condcompstr.c_str());
          unsigned nbaskedmodes = melt_asked_modes_vector.size();
          if (!condcompstr.empty())
            for (unsigned modix=0; modix<nbaskedmodes; modix++)
              if (melt_asked_modes_vector[modix] == condmodstr)
                {
                  MELT_LOCATION_HERE_PRINTF
                  (curlocbuf,
                   "meltgc_load_module_list %s mode-condition %s comp %s line %d: '%s'",
                   modlistpath, condmodstr.c_str(), condcompstr.c_str(), lincnt, modlin);
                  if (condcompstr[0] == '@')
                    meltgc_load_module_list (depth+1, condcompstr.c_str()+1);
                  else
                    meltgc_load_one_module (condcompstr.c_str());
                  break;
                }
        }
      else
        {
          MELT_LOCATION_HERE_PRINTF
          (curlocbuf,
           "meltgc_load_module_list %s plain line %d: '%s'",
           modlistpath, lincnt, modlin);
          melt_debugeprintf ("meltgc_load_module_list depth %d module '%s'", depth, modlin);
          (void) meltgc_load_one_module (modlin);
        }
      MELT_LOCATION_HERE_PRINTF
      (curlocbuf,
       "meltgc_load_module_list %s done line %d: %s",
       modlistpath, lincnt, modlin);
    };
  free (modlin), modlin = NULL;
  fclose (filmod), filmod = NULL;
  goto end;
end:
  MELT_EXITFRAME ();
  if (modlistfull)
    free(modlistfull), modlistfull = NULL;
  if (modlin)
    free (modlin), modlin = NULL;
  if (modlistpath)
    free (modlistpath), modlistpath = NULL;
  return;
}






static void
meltgc_load_modules_and_do_mode (void)
{
  char *curmod = NULL;
  char *nextmod = NULL;
  const char*inistr = NULL;
  const char* xtrastr = NULL;
  char *dupmodpath = NULL;
  int lastmodix = 0;
  char locbuf[200];
  memset(locbuf, 0, sizeof(locbuf));
  MELT_ENTERFRAME(1, NULL);
#define modatv     meltfram__.mcfr_varptr[0]
  inistr = melt_argument ("init");
  melt_debugeprintf ("meltgc_load_modules_and_do_mode startinistr %s",
                     inistr);
  if (melt_asked_modes_vector.empty())
    {
      melt_debugeprintf ("meltgc_load_modules_and_do_mode do nothing without mode (inistr=%s)",
                         inistr);
      goto end;
    }
  /* if there is no -fmelt-init use the default list of modules */
  if (!inistr || !inistr[0])
    {
      inistr = "@@";
      melt_debugeprintf ("meltgc_load_modules_and_do_mode inistr set to default %s", inistr);
    }
  dupmodpath = xstrdup (inistr);
  xtrastr = melt_argument ("extra");
  melt_debugeprintf ("meltgc_load_modules_and_do_mode xtrastr %s", xtrastr);
  modatv = NULL;
  /**
   * first we load all the initial modules
   **/
  curmod = dupmodpath;
  while (curmod && curmod[0])
    {
      nextmod = strchr (curmod, ':');
      if (nextmod)
        {
          *nextmod = (char) 0;
          nextmod++;
        }
      melt_debugeprintf ("meltgc_load_modules_and_do_mode curmod %s before", curmod);
      MELT_LOCATION_HERE_PRINTF
      (locbuf, "meltgc_load_modules_and_do_mode before loading curmod %s",
       curmod);
      if (!strcmp(curmod, "@@"))
        {
          /* the @@ notation means the initial module list; it should
             always be first. */
          if (Melt_Module::nb_modules() >0)
            melt_fatal_error ("MELT default module list should be loaded at first (%d modules already loaded)!",
                              Melt_Module::nb_modules());
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loading default module list %s",
                             melt_default_modlis);
          meltgc_load_module_list (0, melt_default_modlis);
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loaded default module list %s",
                             melt_default_modlis);
        }
      else if (curmod[0] == '@')
        {
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loading given module list %s", curmod+1);
          meltgc_load_module_list (0, curmod+1);
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loaded given module list %s", curmod+1);
        }
      else
        {
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loading given single module %s", curmod);
          meltgc_load_one_module (curmod);
          melt_debugeprintf ("meltgc_load_modules_and_do_mode loaded given single module %s", curmod);
        }
      melt_debugeprintf ("meltgc_load_modules_and_do_mode done curmod %s", curmod);
      curmod = nextmod;
    }
  /**
   * Then we start all the initial modules
   **/
  melt_debugeprintf ("meltgc_load_modules_and_do_mode before starting all new modules modatv=%p", (void*) modatv);
  modatv = meltgc_start_all_new_modules ((melt_ptr_t) modatv);
  melt_debugeprintf ("meltgc_load_modules_and_do_mode started all new modules modatv=%p",  (void*) modatv);

  /* Then we load and start every extra module, if given */
  melt_debugeprintf ("meltgc_load_modules_and_do_mode xtrastr %s lastmodix #%d",
                     xtrastr, lastmodix);
  if (xtrastr && xtrastr[0])
    {
      char* dupxtra = xstrdup (xtrastr);
      char *curxtra = 0;
      char *nextxtra = 0;
      for (curxtra = dupxtra; curxtra && *curxtra; curxtra = nextxtra)
        {
          nextxtra = strchr (curxtra, ':');
          if (nextxtra)
            {
              *nextxtra = (char) 0;
              nextxtra++;
            }
          melt_debugeprintf
          ("meltgc_load_modules_and_do_mode before loading curxtra %s",
           curxtra);
          if (curxtra[0] == '@' && curxtra[1])
            {
              MELT_LOCATION_HERE_PRINTF
              (locbuf,
               "meltgc_load_modules_and_do_mode before extra modlist %s",
               curxtra);
              meltgc_load_module_list (0, curxtra+1);
            }
          else
            {
              MELT_LOCATION_HERE_PRINTF
              (locbuf,
               "meltgc_load_modules_and_do_mode before single extra %s",
               curxtra);
              meltgc_load_one_module (curxtra);
            }
          /* Start all the new loaded modules. */
          modatv = meltgc_start_all_new_modules ((melt_ptr_t) modatv);
          melt_debugeprintf ("meltgc_load_modules_and_do_mode done curxtra %s",
                             curxtra);
        } /* end for curxtra */
    }
  /**
   * then we do all the modes if needed
   **/
  if (melt_get_inisysdata (MELTFIELD_SYSDATA_MODE_DICT)
      && !melt_asked_modes_vector.empty())
    {
      unsigned nbaskedmodes = melt_asked_modes_vector.size();
      for (unsigned modix=0; modix<nbaskedmodes; modix++)
        {
          std::string curmodstr = melt_asked_modes_vector[modix];
          MELT_LOCATION_HERE_PRINTF
          (locbuf,
           "meltgc_load_modules_and_do_mode before #%u initial mode %s", modix, curmodstr.c_str());
          melthookproc_HOOK_MELT_DO_INITIAL_MODE((melt_ptr_t) modatv, curmodstr.c_str());
          melt_debugeprintf
          ("meltgc_load_modules_and_do_mode after doing #%u initial mode %s", modix, curmodstr.c_str());
          MELT_LOCATION_HERE_PRINTF
          (locbuf, "meltgc_load_modules_and_do_mode done #%u initial mode %s", modix, curmodstr.c_str());
        }
    }
end:
  MELT_EXITFRAME ();
#undef modatv
  if (dupmodpath)
    free (dupmodpath), dupmodpath = NULL;
}


/* The low level SIGIO signal handler installed thru sigaction, when
   IO is possible on input channels.  Actual signal handling is done
   at safe places thru MELT_CHECK_SIGNAL & melt_handle_signal &
   meltgc_handle_sigio (because signal handlers can call very few
   async-signal-safe functions, see signal(7) man page on
   e.g. Linux). */
static void
melt_raw_sigio_signal(int sig)
{
  gcc_assert (sig == SIGIO || sig == SIGPIPE);
  melt_got_sigio = 1;
  melt_signaled = 1;
}


/* The low level SIGALRM/SIGVTALRM signal handler installed thru
   sigaction, when an alarm ringed.  Actual signal handling is done
   at safe places thru MELT_CHECK_SIGNAL & melt_handle_signal &
   meltgc_handle_sigalrm (because signal handlers can call very few
   async-signal-safe functions, see signal(7) man page on
   e.g. Linux).  */
static void
melt_raw_sigalrm_signal(int sig)
{
  gcc_assert (sig == SIGALRM || sig == SIGVTALRM);
  melt_got_sigalrm = 1;
  melt_signaled = 1;
}


/* The low level SIGCHLD signal handler installed thru sigaction, when
   a child process exits.  Actual signal handling is done at safe
   places thru MELT_CHECK_SIGNAL & melt_handle_signal &
   meltgc_handle_sigalrm (because signal handlers can call very few
   async-signal-safe functions, see signal(7) man page on
   e.g. Linux).  */
static void
melt_raw_sigchld_signal(int sig)
{
  gcc_assert (sig == SIGCHLD);
  melt_got_sigchld = 1;
  melt_signaled = 1;
}


static void
melt_install_signal_handlers (void)
{
  signal (SIGALRM, melt_raw_sigalrm_signal);
  signal (SIGVTALRM, melt_raw_sigalrm_signal);
  signal (SIGIO, melt_raw_sigio_signal);
  signal (SIGPIPE, melt_raw_sigio_signal);
  signal (SIGCHLD, melt_raw_sigchld_signal);
  melt_debugeprintf ("melt_install_signal_handlers install handlers for SIGIO %d, SIGPIPE %d, SIGALRM %d, SIGVTALRM %d SIGCHLD %d",
                     SIGIO, SIGPIPE, SIGALRM, SIGVTALRM, SIGCHLD);
}

long
melt_relative_time_millisec (void)
{
  struct timeval tv = {0,0};
  errno = 0;
  if (gettimeofday (&tv, NULL))
    melt_fatal_error ("MELT cannot call gettimeofday - %s", xstrerror(errno));
  return (long)(tv.tv_sec - melt_start_time.tv_sec)*1000L
         + (long)(tv.tv_usec - melt_start_time.tv_usec)/1000L;
}

long
melt_cpu_time_millisec (void)
{

  struct timespec ts = {0,0};
  if (clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &ts))
    melt_fatal_error ("MELT cannot call clock_gettime CLOCK_PROCESS_CPUTIME_ID - %s",
                      xstrerror(errno));
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void
melt_set_real_timer_millisec (long millisec)
{
#define MELT_MINIMAL_TIMER_MILLISEC 50
  struct itimerval itv;
  itv.it_interval.tv_sec = 0;
  itv.it_interval.tv_usec = 0;
  itv.it_value.tv_sec = 0;
  itv.it_value.tv_usec = 0;
  if (millisec > 0)
    {
      if (millisec < MELT_MINIMAL_TIMER_MILLISEC)
        millisec = MELT_MINIMAL_TIMER_MILLISEC;
      itv.it_value.tv_sec = millisec / 1000;
      itv.it_value.tv_usec = (millisec % 1000) * 1000;
    };
  if (setitimer (ITIMER_REAL, &itv, NULL))
    melt_fatal_error ("MELT cannot set real timer to %ld millisec - %s",
                      millisec, xstrerror(errno));
}


/****
 * Initialize melt.  Called from toplevel.c before pass management.
 * Should become the MELT plugin initializer.
 ****/
static void
melt_really_initialize (const char* pluginame, const char*versionstr)
{
  static int inited;
  long seed = 0;
  long randomseednum = 0;
  const char *modstr = NULL;
  const char *inistr = NULL;
  const char *countdbgstr = NULL;
  const char *printset = NULL;
  struct stat mystat;
  if (inited)
    return;
  modstr = melt_argument ("mode");
  melt_plugin_gcc_version =  &gcc_version; /* from plugin-version.h */
  melt_debugeprintf ("melt_really_initialize pluginame '%s' versionstr '%s'", pluginame, versionstr);
  melt_debugeprintf ("melt_really_initialize update_path(\"plugins\", \"GCC\")=%s",
                     update_path ("plugins","GCC"));
  gcc_assert (pluginame && pluginame[0]);
  gcc_assert (versionstr && versionstr[0]);
  errno = 0;
  if (gettimeofday (&melt_start_time, NULL))
    melt_fatal_error ("MELT cannot call gettimeofday for melt_start_time (%s)", xstrerror(errno));
  melt_debugeprintf ("melt_really_initialize melt_start_time=%ld",
                     (long) melt_start_time.tv_sec);
  const char*verbosefullgcarg = melt_argument("verbose-full-gc");
  if (!quiet_flag) melt_verbose_full_gc = true;
  if (verbosefullgcarg
      && (verbosefullgcarg[0] == 'Y' || verbosefullgcarg[0] == 'y'
          || verbosefullgcarg[0] == '1'))
    melt_verbose_full_gc = true;
  if (verbosefullgcarg
      && (verbosefullgcarg[0] == 'N' || verbosefullgcarg[0] == 'n'
          || verbosefullgcarg[0] == '0'))
    melt_verbose_full_gc = false;
  if (melt_verbose_full_gc)
    inform(UNKNOWN_LOCATION, "MELT enabled verbose full garbage collection");
  else if (!quiet_flag && !melt_verbose_full_gc)
    inform(UNKNOWN_LOCATION, "MELT disabled verbose full garbage collection");

#if ENABLE_GC_ALWAYS_COLLECT
  /* the GC will be tremendously slowed since called much too often. */
  inform (UNKNOWN_LOCATION,
          "GCC with ENABLE_GC_ALWAYS_COLLECT will slow down MELT [%s]",
          versionstr);
#endif

  // process the mmap-reserve argument; it is the size in megabytes to
  // reserve memory to fill the virtual address space. Only useful for
  // debugging the runtime GC, e.g. helpful for heisenbugs that
  // happens with ASLR only
  const char*mapresvarg = melt_argument("mmap-reserve");
  if (MELT_UNLIKELY(mapresvarg != NULL))
    {
      int megabytereserve = atoi(mapresvarg);
      if (megabytereserve > 0)
        {
          size_t reservesize = (long)megabytereserve << 20;
          void* reservead = mmap(NULL, reservesize, PROT_READ,
                                 MAP_PRIVATE
#ifdef MAP_NORESERVE
                                 | MAP_NORESERVE
#endif /*MAP_NORESERVE*/
                                 | MAP_ANONYMOUS,
                                 (int)-1 /*fd*/,
                                 (off_t)0);
          if (reservead == MAP_FAILED)
            fatal_error (UNKNOWN_LOCATION,
                         "MELT runtime failed to mmap-reserve %d megabytes (%s)",
                         megabytereserve, xstrerror(errno));
          else
            inform (UNKNOWN_LOCATION, "MELT runtime reserved %d megabytes @%p"
#ifdef MAP_NORESERVE
                    " using MAP_NORESERVE"
#endif
                    " at initialization",
                    megabytereserve, reservead);
        }
    }

  Melt_Module::initialize ();
  melt_payload_initialize_static_descriptors ();


  /* when MELT is a plugin, we need to process the debug
     argument. When MELT is a branch, the melt_argument function is
     using melt_flag_debug for "debug" so we don't want this. */
  {
    const char *dbgstr = melt_argument ("debug");
    const char *debuggingstr = melt_argument ("debugging");
    /* debug=n or debug=0 is handled as no debug */
    if (dbgstr && (!dbgstr[0] || !strchr("Nn0", dbgstr[0])))
      {
        inform (UNKNOWN_LOCATION,
                "MELT plugin option -fplugin-arg-melt-debug is obsolete, same as -fplugin-arg-melt-debugging=mode");
        melt_flag_debug = 0;
        melt_debugging_after_mode = 1;
        inform (UNKNOWN_LOCATION,
                "MELT plugin will give debugging messages after mode processing"
                " with obsolete -fplugin-arg-melt-debug. Use -fplugin-arg-melt-debugging=mode instead.");
      }
    if (debuggingstr && debuggingstr[0]
        && debuggingstr[0] != '0' && debuggingstr[0] != 'N' && debuggingstr[0] != 'n')
      {
        if (!strcmp (debuggingstr, "all"))
          {
            inform (UNKNOWN_LOCATION,
                    "MELT plugin is giving all debugging messages.");
            melt_flag_debug = 1;

          }
        else if (!strcmp (debuggingstr, "mode"))
          {
            melt_flag_debug = 0;
            melt_debugging_after_mode = 1;
            inform (UNKNOWN_LOCATION,
                    "MELT plugin will give debugging messages after mode processing.");
          }
        else
          {
            warning (0,
                     "MELT plugin gets unrecognized -fmelt-arg-plugin-debugging=%s option, "
                     "expects 'mode','all', or 'no'",
                     debuggingstr);
          }
      }
  }
  /* When MELT is a plugin, we need to process the bootstrapping
     argument. When MELT is a branch, the melt_argument function is
     using melt_flag_bootstrapping for "bootstrapping" so we don't
     want this.  Likewise for "generate-work-link" &
     melt_flag_generate_work_link, and "keep-temporary-files" &
     melt_flag_keep_temporary_files. */
  {
    const char *bootstr = melt_argument ("bootstrapping");
    const char *genworkdir = melt_argument ("generate-workdir");
    const char *keeptemp = melt_argument("keep-temporary-files");
    /* debug=n or debug=0 is handled as no debug */
    if (bootstr && (!bootstr[0] || !strchr("Nn0", bootstr[0])))
      melt_flag_bootstrapping = 1;
    if (genworkdir && (!genworkdir[0] || !strchr("Nn0", genworkdir[0])))
      melt_flag_generate_work_link = 1;
    if (keeptemp && (!keeptemp[0] || !strchr("Nn0", keeptemp[0])))
      melt_flag_keep_temporary_files = 1;
  }


  /* Ensure that melt_source_dir & melt_module_dir are non-empty paths
     and accessible directories.  Otherwise, this file has been
     miscompiled, or something strange happens, so we issue a warning.  */
  errno = 0;
  gcc_assert (melt_source_dir[0]);
  gcc_assert (melt_module_dir[0]);
  melt_debugeprintf ("melt_really_initialize builtin melt_source_dir %s", melt_source_dir);
  melt_debugeprintf ("melt_really_initialize builtin melt_module_dir %s", melt_module_dir);
  memset (&mystat, 0, sizeof(mystat));
  if (!melt_flag_bootstrapping
      && ((errno=ENOTDIR), (stat(melt_source_dir, &mystat) || !S_ISDIR(mystat.st_mode))))
    warning (0, "MELT with bad builtin source directory %s : %s",
             melt_source_dir, xstrerror (errno));
  memset (&mystat, 0, sizeof(mystat));
  if (!melt_flag_bootstrapping
      && ((errno=ENOTDIR), (stat(melt_module_dir, &mystat) || !S_ISDIR(mystat.st_mode))))
    warning (0, "MELT with bad builtin module directory %s : %s",
             melt_module_dir, xstrerror (errno));
  /* Ensure that the module makefile exists.  */
  gcc_assert (melt_module_make_command[0]);
  gcc_assert (melt_module_makefile[0]);
  if (!melt_flag_bootstrapping && access(melt_module_makefile, R_OK))
    warning (0, "MELT cannot access module makefile %s : %s",
             melt_module_makefile, xstrerror (errno));
  errno = 0;

  /* Open the list of generated C files */
  {
    const char* genfilpath = melt_argument("generated-c-file-list");
    if (genfilpath && genfilpath[0])
      {
        time_t now = 0;
        time (&now);
        melt_generated_c_files_list_fil = fopen(genfilpath, "w");
        if (!melt_generated_c_files_list_fil)
          melt_fatal_error ("failed to open file %s for generated C file list [%s]",
                            genfilpath, xstrerror (errno));
        fprintf (melt_generated_c_files_list_fil,
                 "# file %s with list of [over-]written generated emitted C files\n",
                 genfilpath);
        fprintf (melt_generated_c_files_list_fil,
                 "# MELT version %s run at %s", MELT_VERSION_STRING,
                 /* ctime adds the ending newline! */
                 ctime (&now));
        fprintf (melt_generated_c_files_list_fil,
                 "# unchanged files are prefixed with =, new files are prefixed with +\n");
      }
  }

  /* These are probably never freed! */
  melt_gccversionstr = concat (versionstr, " MELT_",
                               MELT_VERSION_STRING, NULL);
  melt_plugin_name = xstrdup (pluginame);
  inistr = melt_argument ("init");
  countdbgstr = melt_argument ("debugskip");
  ///////
  printset = melt_argument ("print-settings");
  if (printset)
    {
      /* If asked for print-settings, output the settings in a format
         which should be sourcable by a Posix shell. */
      FILE *setfil = NULL;
      time_t now = 0;
      char nowbuf[32];
      char *curlocale = setlocale (LC_ALL, NULL);
      time (&now);
      memset (nowbuf, 0, sizeof nowbuf);
      strncpy (nowbuf, ctime (&now), sizeof(nowbuf)-1);
      {
        char *pcnl = strchr(nowbuf, '\n');
        if (pcnl)
          *pcnl = 0;
      }
      if (!printset[0] || !strcmp(printset, "-"))
        setfil = stdout;
      else
        {
          setfil = fopen (printset, "w");
          if (!setfil)
            melt_fatal_error ("MELT cannot open print-settings file %s : %m",
                              printset);
        }
      fprintf (setfil, "## MELT builtin settings %s\n",
               printset[0]?printset:"-");
      fprintf (setfil, "### see http://gcc-melt.org/\n");
      /* Print the information with a # prefix, so a shell would ignore it.  */
      melt_print_version_info (setfil, "# ");
      /* We don't quote or escape builtin directories path, since they
         should not contain strange characters... */
      fprintf (setfil, "MELTGCCBUILTIN_SOURCE_DIR='%s'\n", melt_source_dir);
      fprintf (setfil, "MELTGCCBUILTIN_MODULE_DIR='%s'\n", melt_module_dir);
      fprintf (setfil, "MELTGCCBUILTIN_MODULE_MAKE_COMMAND='%s'\n",
               melt_module_make_command);
      fprintf (setfil, "MELTGCCBUILTIN_MODULE_MAKEFILE='%s'\n",
               melt_module_makefile);
      fprintf (setfil, "MELTGCCBUILTIN_MODULE_CFLAGS='%s'\n",
               melt_module_cflags);
      fprintf (setfil, "MELTGCCBUILTIN_DEFAULT_MODLIS='%s'\n",
               melt_default_modlis);
      fprintf (setfil, "MELTGCCBUILTIN_GCC_VERSION=%d\n",
               melt_gcc_version);
      fprintf (setfil, "MELTGCCBUILTIN_VERSION='%s'\n",
               MELT_VERSION_STRING);
      fprintf (setfil, "MELTGCCBUILTIN_VERSION_STRING='%s'\n",
               melt_version_str ());
      fprintf (setfil, "MELTGCCBUILTIN_RUNTIME_BUILD_DATE='%s'\n",
               melt_runtime_build_date);
      fprintf (setfil, "MELTGCCBUILTIN_PLUGIN_NAME='%s'\n",
               pluginame);
      fprintf (setfil, "MELTGCCBUILTIN_MELTRUN_PREPROCESSED_MD5='%s'\n",
               melt_run_preprocessed_md5);
      fprintf (setfil, "MELTGCCBUILTIN_GENERATED='%s'\n",
               nowbuf);
      if (gcc_exec_prefix)
        fprintf (setfil, "MELTGCCBUILTIN_GCC_EXEC_PREFIX='%s'\n",
                 gcc_exec_prefix);
      else
        fprintf (setfil, "# MELTGCCBUILTIN_GCC_EXEC_PREFIX is not set\n");
#ifdef __cplusplus
      fprintf (setfil, "MELTGCCBUILTIN_BUILD_WITH_CXX=1\n");
#else
#error MELT needs to be built with a C++ compiler
      fprintf (setfil, "MELTGCCBUILTIN_BUILD_WITH_CXX=O\n");
#endif /*__cplusplus*/
      fflush (setfil);
      if (setfil == stdout)
        inform (UNKNOWN_LOCATION,
                "MELT printed builtin settings on <stdout>");
      else
        {
          inform (UNKNOWN_LOCATION,
                  "MELT printed builtin settings in %s", printset);
          fclose (setfil);
          setfil = 0;
        }
      if (curlocale && curlocale[0]
          && strcmp(curlocale, "C") && strcmp(curlocale, "POSIX"))
        warning(0, "MELT printed settings in %s locale, better use C or POSIX locale!",
                curlocale);
    }

  /* Give messages for bootstrapping about ignored directories. */
  if (melt_flag_bootstrapping)
    {
      char* envpath = NULL;
      inform  (UNKNOWN_LOCATION,
               "MELT is bootstrapping so ignore builtin source directory %s",
               melt_source_dir);
      inform  (UNKNOWN_LOCATION,
               "MELT is bootstrapping so ignore builtin module directory %s",
               melt_module_dir);
      if ((envpath = getenv ("GCCMELT_SOURCE_PATH")) != NULL)
        inform  (UNKNOWN_LOCATION,
                 "MELT is bootstrapping so ignore GCCMELT_SOURCE_PATH=%s",
                 envpath);
      if ((envpath = getenv ("GCCMELT_MODULE_PATH")) != NULL)
        inform  (UNKNOWN_LOCATION,
                 "MELT is bootstrapping so ignore GCCMELT_MODULE_PATH=%s",
                 envpath);

    }

  fflush (stderr);
  fflush (stdout);

  /* Return immediately if no mode is given.  */
  if (!modstr || *modstr=='\0')
    {
      if (!melt_flag_bootstrapping && !printset)
        inform  (UNKNOWN_LOCATION,
                 "MELT don't do anything without a mode; try -fplugin-arg-melt-mode=help");
      melt_debugeprintf ("melt_really_initialize return immediately since no mode (inistr=%s)",
                         inistr);
      return;
    }

  /* Transform the colon-separated modstr into the vector of strings melt_asked_modes_vector */
  {
    const char* colon = NULL, *curmodstr = NULL;
    std::string curmod;
    for (curmodstr = modstr ;
         curmodstr != NULL && (colon=strchr(curmodstr, ':'), *curmodstr);
         curmodstr = colon?(colon+1):NULL)
      {
        if (colon)
          curmod.assign(curmodstr, colon-curmodstr-1);
        else
          curmod.assign(curmodstr);
        melt_asked_modes_vector.push_back (curmod);
        melt_debugeprintf("melt_really_initialize curmod='%s' #%d", curmod.c_str(),
                          (int)melt_asked_modes_vector.size());
      }
  }

  /* Notice if the locale is not UTF-8 */
  if (!locale_utf8 /* from intl.h */)
    inform (UNKNOWN_LOCATION,
            "MELT {%s} prefers an UTF-8 locale for some features, e.g. JSONRPC, but locale is %s",
            melt_version_str(), setlocale(LC_ALL, NULL));

  /* Optionally trace the dynamic linking of modules.  */
  {
    char* moduleenv = getenv ("GCCMELT_TRACE_MODULE");
    if (moduleenv)
      {
        melt_trace_module_fil = fopen (moduleenv, "a");
        if (melt_trace_module_fil)
          {
            const char *outarg = melt_argument ("output");
            time_t now = 0;
            time (&now);
            fprintf (melt_trace_module_fil, "# MELT module tracing start pid %d MELT version %s mode %s at %s",
                     (int) getpid(), MELT_VERSION_STRING, modstr, ctime (&now));
            if (outarg)
              fprintf (melt_trace_module_fil, "# MELT output argument %s\n", outarg);
            fflush (melt_trace_module_fil);
            inform (UNKNOWN_LOCATION,
                    "MELT tracing module loading in %s (GCCMELT_TRACE_MODULE environment variable)",
                    moduleenv);
          }
      }
  }

  /* Optionally trace the source files search.  */
  {
    char *sourceenv = getenv ("GCCMELT_TRACE_SOURCE");
    if (sourceenv)
      {
        melt_trace_source_fil = fopen (sourceenv, "a");
        if (melt_trace_source_fil)
          {
            const char *outarg = melt_argument ("output");
            time_t now = 0;
            time (&now);
            fprintf (melt_trace_source_fil, "# MELT source tracing start pid %d MELT version %s mode %s at %s",
                     (int) getpid(), MELT_VERSION_STRING, modstr, ctime (&now));
            if (outarg)
              fprintf (melt_trace_source_fil, "# MELT output argument %s\n", outarg);
            fflush (melt_trace_source_fil);
            inform (UNKNOWN_LOCATION,
                    "MELT tracing source loading in %s (GCCMELT_TRACE_SOURCE environment variable)",
                    sourceenv);
          }
      }
  }

  if (melt_minorsizekilow == 0)
    {
      const char* minzstr = melt_argument ("minor-zone");
      melt_minorsizekilow = minzstr ? (atol (minzstr)) : 0;
      if (melt_minorsizekilow < 256)
        melt_minorsizekilow = 256;
      else if (melt_minorsizekilow > 32768)
        melt_minorsizekilow = 32768;
    }

  /* The program handle dlopen is not traced! */
  proghandle = dlopen (NULL, RTLD_NOW | RTLD_GLOBAL);
  if (!proghandle)
    {
      const char*dle = dlerror();
      if (!dle) dle="??";
      static char dlbuf[256];
      strncpy(dlbuf, dle, sizeof(dlbuf)-1);
      /* Don't call melt_fatal_error - we are initializing! */
      fatal_error (UNKNOWN_LOCATION,
                   "MELT failed to get whole program handle - %s",
                   dlbuf);
    }

  if (countdbgstr != (char *) 0)
    melt_debugskipcount = atol (countdbgstr);
  randomseednum = get_random_seed (false);
  gcc_assert (MELT_ALIGN == sizeof (void *)
              || MELT_ALIGN == 2 * sizeof (void *)
              || MELT_ALIGN == 4 * sizeof (void *));
  inited = 1;
  ggc_collect ();
  obstack_init (&melt_bstring_obstack);
  obstack_init (&melt_bname_obstack);
  seed = ((seed * 35851) ^ (randomseednum * 65867));
  srand48 (seed);
  gcc_assert (!melt_curalz);
  {
    size_t wantedwords = melt_minorsizekilow * 4096;
    if (wantedwords < (1 << 20))
      wantedwords = (1 << 20);
    gcc_assert (melt_startalz == NULL && melt_endalz == NULL);
    gcc_assert (wantedwords * sizeof (void *) >
                300 * MELTGLOB__LASTGLOB * sizeof (struct meltobject_st));
    melt_allocate_young_gc_zone (wantedwords / sizeof(void*));
    melt_newspecdatalist = NULL;
    melt_oldspecdatalist = NULL;
    melt_debugeprintf ("melt_really_initialize alz %p-%p (%ld Kw)",
                       melt_startalz, melt_endalz, (long) wantedwords >> 10);
  }
  /* Install the signal handlers, even if the signals won't be
     sent. */
  melt_install_signal_handlers ();
  /* We are using register_callback here, even if MELT is not compiled
     as a plugin. */
  register_callback (melt_plugin_name, PLUGIN_GGC_MARKING,
                     melt_marking_callback,
                     NULL);
  register_callback (melt_plugin_name, PLUGIN_GGC_START,
                     melt_ggcstart_callback,
                     NULL);
  register_callback (melt_plugin_name, PLUGIN_ATTRIBUTES,
                     melt_attribute_callback,
                     NULL);
  register_callback (melt_plugin_name, PLUGIN_FINISH,
                     melt_finishall_callback,
                     NULL);
  melt_debugeprintf ("melt_really_initialize cpp_PREFIX=%s", cpp_PREFIX);
  melt_debugeprintf ("melt_really_initialize cpp_EXEC_PREFIX=%s", cpp_EXEC_PREFIX);
  melt_debugeprintf ("melt_really_initialize gcc_exec_prefix=%s", gcc_exec_prefix);
  melt_debugeprintf ("melt_really_initialize melt_source_dir=%s", melt_source_dir);
  melt_debugeprintf ("melt_really_initialize melt_module_dir=%s", melt_module_dir);
  melt_debugeprintf ("melt_really_initialize inistr=%s", inistr);
  /* I really want meltgc_make_special to be linked in, even in plugin
     mode... So I test that the routine exists! */
  melt_debugeprintf ("melt_really_initialize meltgc_make_special=%#lx",
                     (long) meltgc_make_special);
  meltgc_load_modules_and_do_mode ();
  /* force a minor GC */
  melt_garbcoll (0, MELT_ONLY_MINOR);
  if (melt_debugging_after_mode)
    {
      melt_flag_debug = 1;
      melt_debugeprintf ("melt_really_initialize is debugging after mode=%s", modstr);
    };
  melt_debugeprintf ("melt_really_initialize ended init=%s mode=%s",
                     inistr, modstr);
  if (!quiet_flag)
    {
#if MELT_IS_PLUGIN
      fprintf (stderr, "MELT plugin {%s} initialized for mode %s [%d modules]\n",
               versionstr, modstr, Melt_Module::nb_modules());
#else
      fprintf (stderr, "MELT branch {%s} initialized for mode %s [%d modules]\n",
               versionstr, modstr, Melt_Module::nb_modules());
#endif /*MELT_IS_PLUGIN*/
      fflush (stderr);
    }
}



static void
melt_do_finalize (void)
{
  static int didfinal;
  const char* modstr = NULL;
  int arrcount = 0;
#define finclosv meltfram__.mcfr_varptr[0]
  melt_debugeprintf ("melt_do_finalize didfinal %d start", didfinal);
  if (didfinal++>0)
    goto end;
  modstr = melt_argument ("mode");
  melt_debugeprintf ("melt_do_finalize modstr %s", modstr);
  if (!modstr)
    goto end;
  melthookproc_HOOK_EXIT_FINALIZER ();
  /* Always force a minor GC to be sure nothing stays in young
     region.  */
  melt_garbcoll (0, MELT_ONLY_MINOR);
  melt_debugeprintf("melt_do_finalize melt_tempdir %s melt_made_tempdir %d",
                    melt_tempdir, melt_made_tempdir);
  /* Clear the temporary directory if needed.  */
  if (melt_tempdir[0])
    {
      DIR *tdir = opendir (melt_tempdir);
      int nbdelfil = 0;
      struct dirent *dent = NULL;
      char**arrent = NULL;
      int arrsize = 0;
      int ix = 0;
      arrsize = 32;
      arrent = (char**)xcalloc (arrsize, sizeof(char*));
      if (!tdir)
        melt_fatal_error ("failed to open tempdir %s %m", melt_tempdir);
      while ((dent = readdir (tdir)) != NULL)
        {
          if (!dent->d_name[0] || dent->d_name[0] == '.')
            /* This skips  '.' & '..' entries and we have no  .* files.  */
            continue;
          if (arrcount+2 >= arrsize)
            {
              int newsize = ((3*arrcount/2+40)|0xf)+1;
              char** oldarr = arrent;
              char** newarr = (char**)xcalloc(newsize, sizeof(char*));
              memcpy (newarr, arrent, arrcount*sizeof(char*));
              free (oldarr);
              arrent = newarr;
              arrsize = newsize;
            }
          arrent[arrcount++] = xstrdup (dent->d_name);
        };
      closedir (tdir);
      melt_debugeprintf ("melt_do_finalize arrcount=%d melt_tempdir %s keeptemp=%d",
                         arrcount, melt_tempdir, melt_flag_keep_temporary_files);
      if (melt_flag_keep_temporary_files)
        {
          if (arrcount > 0)
            inform (UNKNOWN_LOCATION,
                    "MELT forcibly keeping %d temporary files in %s",
                    arrcount, melt_tempdir);
        }
      else
        {
          for (ix = 0; ix < arrcount; ix++)
            {
              char *tfilnam = arrent[ix];
              char *tfilpath = concat (melt_tempdir, "/", tfilnam, NULL);
              static char symlinkpath[512];
              melt_debugeprintf ("melt_do_finalize remove file #%d %s path %s", ix, tfilnam, tfilpath);
              memset (symlinkpath, 0, sizeof(symlinkpath));
              if (readlink (tfilpath, symlinkpath, sizeof(symlinkpath)-1)>0)
                {
                  if (symlinkpath[0]=='/' && !remove (symlinkpath))
                    nbdelfil++;
                }
              if (!remove (tfilpath))
                nbdelfil++;
              arrent[ix] = NULL;
              free (tfilnam);
              free (tfilpath);
            };
        }
      free (arrent), arrent = NULL;
      if (nbdelfil>0)
        inform (UNKNOWN_LOCATION, "MELT removed %d temporary files from %s",
                nbdelfil, melt_tempdir);
      if (melt_made_tempdir && !melt_flag_keep_temporary_files)
        {
          if (rmdir (melt_tempdir))
            /* @@@ I don't know if it should be a warning or a fatal error -
               we are finalizing! */
            warning (0, "failed to rmdir melt tempdir %s with %d directory entries (%s)",
                     melt_tempdir, arrcount, xstrerror (errno));
        }
    }
  if (melt_generated_c_files_list_fil)
    {
      fprintf (melt_generated_c_files_list_fil, "# end of generated C file list\n");
      fclose (melt_generated_c_files_list_fil);
      melt_generated_c_files_list_fil = NULL;
    }
  if (melt_trace_module_fil)
    {
      fprintf (melt_trace_module_fil, "# end of MELT module trace for pid %d\n\n", (int) getpid());
      fclose (melt_trace_module_fil);
      melt_trace_module_fil = NULL;
    }
  if (melt_trace_source_fil)
    {
      fprintf (melt_trace_source_fil, "# end of MELT source trace for pid %d\n\n", (int) getpid());
      fclose (melt_trace_source_fil);
      melt_trace_source_fil = NULL;
    }
  dbgprintf ("melt_do_finalize ended with #%d modules", Melt_Module::nb_modules());
  if (melt_verbose_full_gc || !quiet_flag)
    {
      /* when asked, the GGC collector displays data, so we show
      our various GC reasons count */
      putc ('\n', stderr);
      fprintf (stderr, "MELT did %ld garbage collections; %ld full + %ld minor.\n",
               melt_nb_garbcoll, melt_nb_full_garbcoll, melt_nb_garbcoll - melt_nb_full_garbcoll);
      if (melt_nb_full_garbcoll > 0)
        fprintf (stderr,
                 "MELT full GCs because %ld asked, %ld periodic, %ld threshold, %ld copied.\n",
                 melt_nb_fullgc_because_asked, melt_nb_fullgc_because_periodic,
                 melt_nb_fullgc_because_threshold, melt_nb_fullgc_because_copied);
      /* we also show the successfully run modes. */
      if (melt_done_modes_vector.empty())
        fprintf (stderr,
                 "MELT did not run any modes successfully.\n");
      else if (melt_done_modes_vector.size() == 1)
        fprintf (stderr, "MELT did run one mode %s successfully.\n", melt_done_modes_vector[0].c_str());
      else
        {
          unsigned nbmodes = melt_done_modes_vector.size();
          fprintf (stderr, "MELT did run %d modes successfully:", (int) nbmodes);
          for (unsigned ix=0; ix<nbmodes; ix++)
            {
              if (ix>0)
                fputs (", ", stderr);
              else
                fputc (' ', stderr);
              fputs (melt_done_modes_vector[ix].c_str(), stderr);
            };
          fputs (".\n", stderr);
        }
      if (melt_dbgcounter > 0)
        fprintf (stderr, "MELT final debug counter %ld\n", melt_dbgcounter);
      fflush (stderr);
    }
end:
  melt_done_modes_vector.clear();
  fflush(NULL);
}


#ifdef MELT_IS_PLUGIN
/* this code is GPLv3 licenced & FSF copyrighted, so of course it is a
   GPL compatible GCC plugin. */
int plugin_is_GPL_compatible = 1;


/* the plugin initialization code has to be exactly plugin_init */
int
plugin_init (struct plugin_name_args* plugin_info,
             struct plugin_gcc_version* gcc_version)
{
  char* gccversionstr = NULL;
  gcc_assert (plugin_info != NULL);
  gcc_assert (gcc_version != NULL);
  melt_plugin_argc = plugin_info->argc;
  melt_plugin_argv = plugin_info->argv;
  if (gcc_version->devphase && gcc_version->devphase[0])
    gccversionstr = concat (gcc_version->basever, " ",
                            gcc_version->datestamp, " (",
                            gcc_version->devphase, ") [MELT plugin]",
                            NULL);
  else
    gccversionstr = concat (gcc_version->basever, " ",
                            gcc_version->datestamp, ":[MELT plugin]",
                            NULL);
  if (!plugin_info->version)
    {
      /* this string is never freed */
      plugin_info->version = concat ("MELT ", melt_version_str (), NULL);
    };
  if (!plugin_info->help)
    plugin_info->help =
      "MELT is a meta-plugin providing a high-level \
lispy domain specific language to extend GCC.  See http://gcc-melt.org/";
  melt_really_initialize (plugin_info->base_name, gccversionstr);
  free (gccversionstr);
  melt_debugeprintf ("end of melt plugin_init");
  return 0; /* success */
}

#else /* !MELT_IS_PLUGIN*/
void
melt_initialize (void)
{
  melt_debugeprintf ("start of melt_initialize [builtin MELT] version_string %s",
                     version_string);
  /* For the MELT branch, we are using the plugin facilities without
     calling add_new_plugin, so we need to force the flag_plugin_added
     so that every plugin hook registration runs as if there was a
     MELT plugin!  */
  flag_plugin_added = true;
  melt_really_initialize ("MELT/_builtin", version_string);
  melt_debugeprintf ("end of melt_initialize [builtin MELT] meltruntime %s", __DATE__);
}
#endif  /* MELT_IS_PLUGIN */


int *
melt_dynobjstruct_fieldoffset_at (const char *fldnam, const char *fil,
                                  int lin)
{
  char *nam = 0;
  void *ptr = 0;
  nam = concat ("meltfieldoff__", fldnam, NULL);
  ptr = melt_dlsym_all (nam);
  if (!ptr)
    warning (0,
             "MELT failed to find field offset %s - %s (%s:%d)", nam,
             dlerror (), fil, lin);
  free (nam);
  return (int *) ptr;
}


int *
melt_dynobjstruct_classlength_at (const char *clanam, const char *fil,
                                  int lin)
{
  char *nam = 0;
  void *ptr = 0;
  nam = concat ("meltclasslen__", clanam, NULL);
  ptr = melt_dlsym_all (nam);
  if (!ptr)
    warning (0,
             "MELT failed to find class length %s - %s (%s:%d)", nam,
             dlerror (), fil, lin);
  free (nam);
  return (int *) ptr;
}


/****
 * finalize melt. Called from toplevel.c after all is done
 ****/
void
melt_finalize (void)
{
  melt_do_finalize ();
  melt_debugeprintf ("melt_finalize with %ld GarbColl, %ld fullGc",
                     melt_nb_garbcoll, melt_nb_full_garbcoll);
}




static void
discr_out (struct debugprint_melt_st *dp, meltobject_ptr_t odiscr)
{
  int dmag = melt_magic_discr ((melt_ptr_t) odiscr);
  struct meltstring_st *str = NULL;
  if (dmag != MELTOBMAG_OBJECT)
    {
      fprintf (dp->dfil, "?discr@%p?", (void *) odiscr);
      return;
    }
  if (odiscr->obj_len >= MELTLENGTH_CLASS_NAMED && odiscr->obj_vartab)
    {
      str = (struct meltstring_st *) odiscr->obj_vartab[MELTFIELD_NAMED_NAME];
      if (melt_magic_discr ((melt_ptr_t) str) != MELTOBMAG_STRING)
        str = NULL;
    }
  if (!str)
    {
      fprintf (dp->dfil, "?odiscr/%d?", odiscr->obj_hash);
      return;
    }
  fprintf (dp->dfil, "#%s", str->val);
}


static void
nl_debug_out (struct debugprint_melt_st *dp, int depth)
{
  int i;
  putc ('\n', dp->dfil);
  for (i = 0; i < depth; i++)
    putc (' ', dp->dfil);
}

static void
skip_debug_out (struct debugprint_melt_st *dp, int depth)
{
  if (dp->dcount % 4 == 0)
    nl_debug_out (dp, depth);
  else
    putc (' ', dp->dfil);
}


static bool
is_named_obj (meltobject_ptr_t ob)
{
  struct meltstring_st *str = 0;
  if (melt_magic_discr ((melt_ptr_t) ob) != MELTOBMAG_OBJECT)
    return FALSE;
  if (ob->obj_len < MELTLENGTH_CLASS_NAMED || !ob->obj_vartab)
    return FALSE;
  str = (struct meltstring_st *) ob->obj_vartab[MELTFIELD_NAMED_NAME];
  if (melt_magic_discr ((melt_ptr_t) str) != MELTOBMAG_STRING)
    return FALSE;
  if (melt_is_instance_of ((melt_ptr_t) ob, (melt_ptr_t) MELT_PREDEF (CLASS_NAMED)))
    return TRUE;
  return FALSE;
}

static void
debug_outstr (struct debugprint_melt_st *dp, const char *str)
{
  int nbclin = 0;
  const char *pc;
  for (pc = str; *pc; pc++)
    {
      nbclin++;
      if (nbclin > 60 && strlen (pc) > 5)
        {
          if (ISSPACE (*pc) || ISPUNCT (*pc) || nbclin > 72)
            {
              fputs ("\\\n", dp->dfil);
              nbclin = 0;
            }
        }
      switch (*pc)
        {
        case '\n':
          fputs ("\\n", dp->dfil);
          break;
        case '\r':
          fputs ("\\r", dp->dfil);
          break;
        case '\t':
          fputs ("\\t", dp->dfil);
          break;
        case '\v':
          fputs ("\\v", dp->dfil);
          break;
        case '\f':
          fputs ("\\f", dp->dfil);
          break;
        case '\"':
          fputs ("\\q", dp->dfil);
          break;
        case '\'':
          fputs ("\\a", dp->dfil);
          break;
        default:
          if (ISPRINT (*pc))
            putc (*pc, dp->dfil);
          else
            fprintf (dp->dfil, "\\x%02x", (*pc) & 0xff);
          break;
        }
    }
}


void
melt_debug_out (struct debugprint_melt_st *dp,
                melt_ptr_t ptr, int depth)
{
  int mag = melt_magic_discr (ptr);
  int ix;
  if (!dp->dfil)
    return;
  dp->dcount++;
  switch (mag)
    {
    case 0:
    {
      if (ptr)
        fprintf (dp->dfil, "??@%p??", (void *) ptr);
      else
        fputs ("@@", dp->dfil);
      break;
    }
    case MELTOBMAG_OBJECT:
    {
      struct meltobject_st *p = (struct meltobject_st *) ptr;
      bool named = is_named_obj (p);
      fputs ("%", dp->dfil);
      discr_out (dp, p->meltobj_class);
      fprintf (dp->dfil, "/L%dH%d", p->obj_len, p->obj_hash);
      if (p->obj_num)
        fprintf (dp->dfil, "N%d", p->obj_num);
      if (named)
        fprintf (dp->dfil, "<#%s>",
                 ((struct meltstring_st *) (p->obj_vartab
                                            [MELTFIELD_NAMED_NAME]))->val);
      if ((!named || depth == 0) && depth < dp->dmaxdepth)
        {
          fputs ("[", dp->dfil);
          if (p->obj_vartab)
            for (ix = 0; ix < (int) p->obj_len; ix++)
              {
                if (ix > 0)
                  skip_debug_out (dp, depth);
                melt_debug_out (dp, p->obj_vartab[ix], depth + 1);
              }
          fputs ("]", dp->dfil);
        }
      else if (!named)
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_MULTIPLE:
    {
      struct meltmultiple_st *p = (struct meltmultiple_st *) ptr;
      fputs ("*", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fputs ("(", dp->dfil);
          for (ix = 0; ix < (int) p->nbval; ix++)
            {
              if (ix > 0)
                skip_debug_out (dp, depth);
              melt_debug_out (dp, p->tabval[ix], depth + 1);
            }
          fputs (")", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_STRING:
    {
      struct meltstring_st *p = (struct meltstring_st *) ptr;
      fputs ("!", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fputs ("\"", dp->dfil);
          debug_outstr (dp, p->val);
          fputs ("\"", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_INT:
    {
      struct meltint_st *p = (struct meltint_st *) ptr;
      fputs ("!", dp->dfil);
      discr_out (dp, p->discr);
      fprintf (dp->dfil, "#%ld", p->val);
      break;
    }
    case MELTOBMAG_MIXINT:
    {
      struct meltmixint_st *p = (struct meltmixint_st *) ptr;
      fputs ("!", dp->dfil);
      discr_out (dp, p->discr);
      fprintf (dp->dfil, "[#%ld&", p->intval);
      melt_debug_out (dp, p->ptrval, depth + 1);
      fputs ("]", dp->dfil);
      break;
    }
    case MELTOBMAG_MIXLOC:
    {
      struct meltmixloc_st *p = (struct meltmixloc_st *) ptr;
      fputs ("!", dp->dfil);
      discr_out (dp, p->discr);
      fprintf (dp->dfil, "[#%ld&", p->intval);
      melt_debug_out (dp, p->ptrval, depth + 1);
      fputs ("]", dp->dfil);
      break;
    }
    case MELTOBMAG_LIST:
    {
      struct meltlist_st *p = (struct meltlist_st *) ptr;
      fputs ("!", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          int ln = melt_list_length ((melt_ptr_t) p);
          struct meltpair_st *pr = 0;
          if (ln > 2)
            fprintf (dp->dfil, "[/%d ", ln);
          else
            fputs ("[", dp->dfil);
          for (pr = p->first;
               pr && melt_magic_discr ((melt_ptr_t) pr) == MELTOBMAG_PAIR;
               pr = pr->tl)
            {
              melt_debug_out (dp, pr->hd, depth + 1);
              if (pr->tl)
                skip_debug_out (dp, depth);
            }
          fputs ("]", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_MAPSTRINGS:
    {
      struct meltmapstrings_st *p = (struct meltmapstrings_st *) ptr;
      fputs ("|", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          int ln = melt_primtab[p->lenix];
          fprintf (dp->dfil, "{~%d/", p->count);
          if (p->entab)
            for (ix = 0; ix < ln; ix++)
              {
                const char *ats = p->entab[ix].e_at;
                if (!ats || ats == HTAB_DELETED_ENTRY)
                  continue;
                nl_debug_out (dp, depth);
                fputs ("'", dp->dfil);
                debug_outstr (dp, ats);
                fputs ("' = ", dp->dfil);
                melt_debug_out (dp, p->entab[ix].e_va, depth + 1);
                fputs (";", dp->dfil);
              }
          fputs (" ~}", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_MAPOBJECTS:
    {
      struct meltmapobjects_st *p = (struct meltmapobjects_st *) ptr;
      fputs ("|", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          int ln = melt_primtab[p->lenix];
          fprintf (dp->dfil, "{%d/", p->count);
          if (p->entab)
            for (ix = 0; ix < ln; ix++)
              {
                meltobject_ptr_t atp = p->entab[ix].e_at;
                if (!atp || atp == HTAB_DELETED_ENTRY)
                  continue;
                nl_debug_out (dp, depth);
                melt_debug_out (dp, (melt_ptr_t) atp, dp->dmaxdepth);
                fputs ("' = ", dp->dfil);
                melt_debug_out (dp, p->entab[ix].e_va, depth + 1);
                fputs (";", dp->dfil);
              }
          fputs (" }", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_CLOSURE:
    {
      struct meltclosure_st *p = (struct meltclosure_st *) ptr;
      fputs ("!.", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fprintf (dp->dfil, "[. rout=");
          melt_debug_out (dp, (melt_ptr_t) p->rout, depth + 1);
          skip_debug_out (dp, depth);
          fprintf (dp->dfil, " /%d: ", p->nbval);
          for (ix = 0; ix < (int) p->nbval; ix++)
            {
              if (ix > 0)
                skip_debug_out (dp, depth);
              melt_debug_out (dp, p->tabval[ix], depth + 1);
            }
          fputs (".]", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_ROUTINE:
    {
      struct meltroutine_st *p = (struct meltroutine_st *) ptr;
      fputs ("!:", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fprintf (dp->dfil, ".%s[:/%d ", p->routdescr, p->nbval);
          for (ix = 0; ix < (int) p->nbval; ix++)
            {
              if (ix > 0)
                skip_debug_out (dp, depth);
              melt_debug_out (dp, p->tabval[ix], depth + 1);
            }
          fputs (":]", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_STRBUF:
    {
      struct meltstrbuf_st *p = (struct meltstrbuf_st *) ptr;
      fputs ("!`", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fprintf (dp->dfil, "[`buflen=%ld ", melt_primtab[p->buflenix]);
          gcc_assert (p->bufstart <= p->bufend
                      && p->bufend < (unsigned) melt_primtab[p->buflenix]);
          fprintf (dp->dfil, "bufstart=%u bufend=%u buf='",
                   p->bufstart, p->bufend);
          if (p->bufzn)
            debug_outstr (dp, p->bufzn + p->bufstart);
          fputs ("' `]", dp->dfil);
        }
      else
        fputs ("..", dp->dfil);
      break;
    }
    case MELTOBMAG_PAIR:
    {
      struct meltpair_st *p = (struct meltpair_st *) ptr;
      fputs ("[pair:", dp->dfil);
      discr_out (dp, p->discr);
      if (depth < dp->dmaxdepth)
        {
          fputs ("hd:", dp->dfil);
          melt_debug_out (dp, p->hd, depth + 1);
          fputs ("; ti:", dp->dfil);
          melt_debug_out (dp, (melt_ptr_t) p->tl, depth + 1);
        }
      else
        fputs ("..", dp->dfil);
      fputs ("]", dp->dfil);
      break;
    }
    case MELTOBMAG_TREE:
    case MELTOBMAG_GIMPLE:
    case MELTOBMAG_GIMPLESEQ:
    case MELTOBMAG_BASICBLOCK:
    case MELTOBMAG_EDGE:
    case MELTOBMAG_MAPTREES:
    case MELTOBMAG_MAPGIMPLES:
    case MELTOBMAG_MAPGIMPLESEQS:
    case MELTOBMAG_MAPBASICBLOCKS:
    case MELTOBMAG_MAPEDGES:
    case MELTOBMAG_DECAY:
      melt_fatal_error ("debug_out unimplemented magic %d", mag);
    default:
      melt_fatal_error ("debug_out invalid magic %d", mag);
    }
}



void
melt_dbgeprint (void *p)
{
  struct debugprint_melt_st dps =
  {
    0, 4, 0
  };
  dps.dfil = stderr;
  melt_debug_out (&dps, (melt_ptr_t) p, 0);
  putc ('\n', stderr);
  fflush (stderr);
}



static void
melt_errprint_dladdr(void*ad)
{
  if (!ad) return;
#if _GNU_SOURCE
  /* we have dladdr! */
  Dl_info funinf;
  memset (&funinf, 0, sizeof(funinf));
  if (dladdr (ad, &funinf))
    {
      if (funinf.dli_fname)
        /* Just print the basename of the *.so since it has an
        md5sum in the path.  */
        fprintf (stderr, "\n  %s", melt_basename (funinf.dli_fname));
      if (funinf.dli_sname)
        fprintf (stderr, " [%s=%p]",
                 funinf.dli_sname, funinf.dli_saddr);
      fputc('\n', stderr);
    }
  else
    fputs (" ?", stderr);
#endif /*_GNU_SOURCE*/
}

void
melt_dbgbacktrace (int depth)
{
  int curdepth = 1, totdepth = 0;
  if (depth < 3)
    depth = 3;
  fprintf (stderr, "    <{\n");
  Melt_CallFrame *cfr = NULL;
  for (cfr = (Melt_CallFrame*)melt_top_call_frame;
       cfr != NULL && curdepth < depth;
       (cfr = (Melt_CallFrame*)cfr->previous_frame()), (curdepth++))
    {
      const char* sloc = cfr->srcloc();
      fprintf (stderr, "frame#%d current:", curdepth);
      if (sloc && sloc[0])
        fprintf (stderr, " {%s} ", sloc);
      // for some reason, the checkruntime compilation don't like
      // that.  The bug is elsewehere, but we just avoid the code...
#if MELT_HAVE_DEBUG && !defined(GCCMELT_CHECKMELTRUNTIME)
      else if (cfr->dbg_file())
        fprintf (stderr, " [%s:%d]", cfr->dbg_file(), (int) cfr->dbg_line());
#endif /*MELT_HAVE_DEBUG*/
      else
        fputs (" ", stderr);
      melt_ptr_t current = cfr->current();
      if (current)
        {
          char sbuf[32];
          snprintf (sbuf, sizeof(sbuf), "Frame#%d", curdepth);
          melt_low_stderr_value(sbuf,current);
        }
      else
        fputs("??", stderr);
      putc ('\n', stderr);
      if (curdepth % 4 == 0)
        fflush(stderr);
    }
  for (totdepth = curdepth;
       cfr != NULL;
       cfr = (Melt_CallFrame*)cfr->previous_frame())
    totdepth++;
  fprintf (stderr, "}> backtraced %d frames of %d\n", curdepth, totdepth);
  fflush (stderr);
}


void
melt_dbgshortbacktrace (const char *msg, int maxdepth)
{
  int curdepth = 1;
  if (maxdepth < 5)
    maxdepth = 5;
  fprintf (stderr, "\nSHORT BACKTRACE[#%ld] %s;", melt_dbgcounter,
           msg ? msg : "/");
  Melt_CallFrame *cfr = NULL;
  for (cfr = (Melt_CallFrame*)melt_top_call_frame;
       cfr != NULL && curdepth < maxdepth;
       (cfr = (Melt_CallFrame*)cfr->previous_frame()), (curdepth++))
    {
      fputs ("\n", stderr);
      fprintf (stderr, "#%d:", curdepth);
      meltclosure_ptr_t curclos = NULL;
      melthook_ptr_t curhook = NULL;
      const char* sloc = cfr->srcloc();
      if (sloc && sloc[0])
        fprintf (stderr, "@%s ", sloc);
      // for some reason, the checkruntime fails here. The bug is
      // elsewhere, but we circumvent it...
#if MELT_HAVE_DEBUG  && !defined(GCCMELT_CHECKMELTRUNTIME)
      else if (cfr->dbg_file())
        fprintf (stderr, " [%s:%d]", cfr->dbg_file(), (int) cfr->dbg_line());
#endif /*MELT_HAVE_DEBUG*/
      if ((curclos= cfr->current_closure()) != NULL)
        {
          meltroutine_ptr_t curout = curclos->rout;
          if (melt_magic_discr ((melt_ptr_t) curout) == MELTOBMAG_ROUTINE)
            fprintf (stderr, "<%s> ", curout->routdescr);
          else
            fputs ("?norout?", stderr);
          melt_errprint_dladdr((void*) curout->routfunad);
        }
      else if ((curhook= cfr->current_hook()) != NULL)
        {
          fprintf (stderr, "!<%s> ", curhook->hookname);
          melt_errprint_dladdr((void*) curhook->hookad);
        }
      else
        fputs ("??", stderr);
    }
  if (cfr && maxdepth > curdepth)
    fprintf (stderr, "...&%d", maxdepth - curdepth);
  else
    fputs (".", stderr);
  putc ('\n', stderr);
  putc ('\n', stderr);
  fflush (stderr);
}




void melt_warn_for_no_expected_secondary_results_at (const char*fil, int lin)
{
  static long cnt;
  if (cnt++ > 8)
    return;
  const char* ws = melt_argument ("warn-unexpected-secondary");
  if (!ws || ws[0] == 'N' || ws[0] == 'n' || ws[0] == '0')
    return;
  /* This warning is emitted when a MELT function caller expects
     secondary results, but none are returned.  */
  warning(0,
          "MELT RUNTIME WARNING [#%ld]: Secondary results are expected at %s line %d",
          melt_dbgcounter, fil, lin);
  if (melt_flag_bootstrapping || melt_flag_debug)
    melt_dbgshortbacktrace("MELT caller expected secondary result[s] but got none", 10);
}

/* wrapping gimple & tree prettyprinting for MELT debug */


/* we really need in memory FILE* output; GNU libc -ie Linux- provides
   open_memstream for that; on other systems we use a temporary file,
   which would be very slow if it happens to not be cached in
   memory */

char* meltppbuffer;
size_t meltppbufsiz;
FILE* meltppfile;

#if !HAVE_OPEN_MEMSTREAM && !_GNU_SOURCE
static char* meltppfilename;
#endif


/* open the melttppfile for pretty printing, return the old one */
FILE*
melt_open_ppfile (void)
{
  FILE* oldfile = meltppfile;
#if HAVE_OPEN_MEMSTREAM || _GNU_SOURCE
  meltppbufsiz = 1024;
  meltppbuffer = (char*) xcalloc (1, meltppbufsiz);
  meltppfile = open_memstream (&meltppbuffer, &meltppbufsiz);
  if (!meltppfile)
    melt_fatal_error ("failed to open meltpp file in memory (%s)", xstrerror(errno));
#else
  if (!meltppfilename)
    {
#ifdef MELT_IS_PLUGIN
      /* in plugin mode, make_temp_file is not available from cc1,
      because make_temp_file is defined in libiberty.a and cc1 does
      not use make_temp_file so do not load the make_temp_file.o
      member of the static library libiberty!
      See also http://gcc.gnu.org/ml/gcc/2009-07/msg00157.html
      */
      static char ourtempnamebuf[L_tmpnam+1];
      int tfd = -1;
      strcpy (ourtempnamebuf, "/tmp/meltemp_XXXXXX");
      tfd = mkstemp (ourtempnamebuf);
      if (tfd>=0)
        meltppfilename = ourtempnamebuf;
      else
        melt_fatal_error ("melt temporary file: mkstemp %s failed", ourtempnamebuf);
#else  /* !MELT_IS_PLUGIN */
      meltppfilename = make_temp_file (".meltmem");
      if (!meltppfilename)
        melt_fatal_error ("failed to get melt memory temporary file %s",
                          xstrerror(errno));
#endif  /* MELT_IS_PLUGIN */
    }
  meltppfile = fopen (meltppfilename, "w+");
#endif
  return oldfile;
}

/* close the meltppfile for pretty printing; after than, the
   meltppbuffer & meltppbufsize contains the FILE* content */
void
melt_close_ppfile (FILE *oldfile)
{
  gcc_assert (meltppfile != (FILE*)0);
#if HAVE_OPEN_MEMSTREAM
  /* the fclose automagically updates meltppbuffer & meltppbufsiz */
  fclose (meltppfile);
#else
  /* we don't have an in-memory FILE*; so we read the file; you'll
     better have it in a fast file system, like a memory one. */
  fflush (meltppfile);
  meltppbufsiz = (size_t) ftell (meltppfile);
  rewind (meltppfile);
  meltppbuffer = (char*) xcalloc(1, meltppbufsiz);
  if (fread (meltppbuffer, meltppbufsiz, 1, meltppfile) <= 0)
    melt_fatal_error ("failed to re-read melt buffer temporary file (%s)",
                      xstrerror (errno));
  fclose (meltppfile);
#endif
  meltppfile = oldfile;
}



/* pretty print into an outbuf a gimple */
void
meltgc_ppout_gimple (melt_ptr_t out_p, int indentsp, melt_gimpleptr_t gstmt)
{
  int outmagic = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (1, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  outmagic = melt_magic_discr ((melt_ptr_t) outv);
  if (!gstmt)
    {
      meltgc_add_out ((melt_ptr_t) outv,
                      "%nullgimple%");
      goto end;
    }
  // Nota Bene: passing TDF_VOPS give a crash from an IPA pass like justcount
  switch (outmagic)
    {
    case MELTOBMAG_STRBUF:
    {
      FILE* oldfil = melt_open_ppfile ();
      print_gimple_stmt (meltppfile, gstmt, indentsp,
                         TDF_LINENO | TDF_SLIM);
      melt_close_ppfile (oldfil);
      meltgc_add_out_raw_len ((melt_ptr_t) outv, meltppbuffer, (int) meltppbufsiz);
      free(meltppbuffer);
      meltppbuffer = 0;
      meltppbufsiz = 0;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      FILE* f = melt_get_file ((melt_ptr_t)outv);
      if (!f)
        goto end;
      print_gimple_stmt (f, gstmt, indentsp,
                         TDF_LINENO | TDF_SLIM);
      fflush (f);
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef outv
}

/* pretty print into an outbuf a gimple seq */
void
meltgc_ppout_gimple_seq (melt_ptr_t out_p, int indentsp,
                         melt_gimpleseqptr_t gseq)
{
  int outmagic = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (2, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  if (!gseq)
    {
      meltgc_add_out ((melt_ptr_t) outv,
                      "%nullgimpleseq%");
      goto end;
    }
  outmagic = melt_magic_discr ((melt_ptr_t) outv);
  switch (outmagic)
    {
    // Nota Bene: passing TDF_VOPS give a crash from an IPA pass like justcount
    case MELTOBMAG_STRBUF:
    {
      FILE* oldfil = melt_open_ppfile ();
      print_gimple_seq (meltppfile, gseq, indentsp,
                        TDF_LINENO | TDF_SLIM);
      melt_close_ppfile (oldfil);
      meltgc_add_out_raw_len ((melt_ptr_t) outv, meltppbuffer, (int) meltppbufsiz);
      free(meltppbuffer);
      meltppbuffer = 0;
      meltppbufsiz = 0;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      FILE* f = melt_get_file ((melt_ptr_t)outv);
      if (!f)
        goto end;
      print_gimple_seq (f, gseq, indentsp,
                        TDF_LINENO | TDF_SLIM);
      fflush (f);
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef endv
}

/* pretty print a tree */
void
meltgc_ppout_tree_perhaps_briefly (melt_ptr_t out_p, int indentsp, melt_treeptr_t tr, bool briefly)
{
  int outmagic = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (2, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  if (!tr)
    {
      meltgc_add_out_raw ((melt_ptr_t) outv, "%nulltree%");
      goto end;
    }
  outmagic = melt_magic_discr ((melt_ptr_t) outv);
  switch (outmagic)
    {
    case MELTOBMAG_STRBUF:
    {
      FILE* oldfil = melt_open_ppfile ();
      if (briefly)
        print_node_brief (meltppfile, "", tr, indentsp);
      else
        print_node (meltppfile, "", tr, indentsp);
      melt_close_ppfile (oldfil);
      meltgc_add_out_raw_len ((melt_ptr_t) outv, meltppbuffer, (int) meltppbufsiz);
      free(meltppbuffer);
      meltppbuffer = 0;
      meltppbufsiz = 0;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      FILE* f = melt_get_file ((melt_ptr_t)outv);
      if (!f)
        goto end;
      if (briefly)
        print_node_brief (f, "", tr, indentsp);
      else
        print_node (f, "", tr, indentsp);
      fflush (f);
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef outv
}


/* pretty print into an outbuf a basicblock */
void
meltgc_ppout_basicblock (melt_ptr_t out_p, int indentsp,
                         melt_basicblockptr_t bb)
{
  gimple_seq gsq = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (2, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  if (!bb)
    {
      meltgc_add_out_raw ((melt_ptr_t) outv,
                          "%nullbasicblock%");
      goto end;
    }
  meltgc_out_printf ((melt_ptr_t) outv,
                     "basicblock ix%d", bb->index);
  gsq = bb_seq (bb);
  if (gsq)
    {
      meltgc_add_out_raw ((melt_ptr_t) outv, "{.");
      meltgc_ppout_gimple_seq ((melt_ptr_t) outv,
                               indentsp + 1, gsq);
      meltgc_add_out_raw ((melt_ptr_t) outv, ".}");
    }
  else
    meltgc_add_out_raw ((melt_ptr_t) outv, "_;");
end:
  MELT_EXITFRAME ();
#undef sbufv
}

/* print into an outbuf an edge */
void
meltgc_out_edge (melt_ptr_t out_p, edge edg)
{
  int outmagic = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (1, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  outmagic = melt_magic_discr ((melt_ptr_t) outv);
  if (!edg)
    {
      meltgc_add_out ((melt_ptr_t) outv,
                      "%nulledge%");
      goto end;
    }
  switch (outmagic)
    {
    case MELTOBMAG_STRBUF:
    {
      FILE* oldfil= melt_open_ppfile ();
      dump_edge_info (meltppfile, edg,
#if GCCPLUGIN_VERSION >= 4008
                      TDF_DETAILS,
#endif
                      /*do_succ=*/ 1);
      melt_close_ppfile (oldfil);
      meltgc_add_out_raw_len ((melt_ptr_t) outv, meltppbuffer, (int) meltppbufsiz);
      free(meltppbuffer);
      meltppbuffer = 0;
      meltppbufsiz = 0;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      FILE* f = melt_get_file ((melt_ptr_t)outv);
      if (!f)
        goto end;
      dump_edge_info (f, edg,
                      TDF_DETAILS,
                      /*do_succ=*/ 1);
      fflush (f);
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef outv
}


/* print into an outbuf a loop */
void
meltgc_out_loop (melt_ptr_t out_p, loop_p loo)
{
  int outmagic = 0;
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (1, NULL);
  outv = out_p;
  if (!outv)
    goto end;
  outmagic = melt_magic_discr ((melt_ptr_t) outv);
  if (!loo)
    {
      meltgc_add_out ((melt_ptr_t) outv,
                      "%null_loop%");
      goto end;
    }
  switch (outmagic)
    {
    case MELTOBMAG_STRBUF:
    {
      FILE* oldfil= melt_open_ppfile ();
      fprintf (meltppfile, "loop@%p: ", (void*) loo);
      flow_loop_dump (loo, meltppfile, NULL, 1);
      melt_close_ppfile (oldfil);
      meltgc_add_out_raw_len ((melt_ptr_t) outv, meltppbuffer, (int) meltppbufsiz);
      free(meltppbuffer);
      meltppbuffer = 0;
      meltppbufsiz = 0;
    }
    break;
    case MELTOBMAG_SPECIAL_DATA:
    {
      FILE* f = melt_get_file ((melt_ptr_t)outv);
      if (!f)
        goto end;
      fprintf (f, "loop@%p: ", (void*) loo);
      flow_loop_dump (loo, f, NULL, 1);
      fflush (f);
    }
    break;
    default:
      goto end;
    }
end:
  MELT_EXITFRAME ();
#undef outv
}


/* pretty print into an sbuf a mpz_t GMP multiprecision integer */
void
meltgc_ppout_mpz (melt_ptr_t out_p, int indentsp, mpz_t mp)
{
  int len = 0;
  char* cbuf = 0;
  char tinybuf [64];
#define outv meltfram__.mcfr_varptr[0]
  MELT_ENTERFRAME (2, NULL);
  outv = out_p;
  memset(tinybuf, 0, sizeof (tinybuf));
  if (!outv || indentsp<0)
    goto end;
  if (!mp)
    {
      meltgc_add_out_raw ((melt_ptr_t) outv, "%nullmp%");
      goto end;
    }
  len = mpz_sizeinbase(mp, 10) + 2;
  if (len < (int)sizeof(tinybuf)-2)
    {
      mpz_get_str (tinybuf, 10, mp);
      meltgc_add_out_raw ((melt_ptr_t) outv, tinybuf);
    }
  else
    {
      cbuf = (char*) xcalloc(len+2, 1);
      mpz_get_str(cbuf, 10, mp);
      meltgc_add_out_raw ((melt_ptr_t) outv, cbuf);
      free(cbuf);
    }
end:
  MELT_EXITFRAME ();
#undef sbufv
}


/* pretty print into an out the GMP multiprecision integer of a mixbigint */
void
meltgc_ppout_mixbigint (melt_ptr_t out_p, int indentsp,
                        melt_ptr_t big_p)
{
#define outv meltfram__.mcfr_varptr[0]
#define bigv  meltfram__.mcfr_varptr[1]
  MELT_ENTERFRAME (3, NULL);
  outv = out_p;
  bigv = big_p;
  if (!outv)
    goto end;
  if (!bigv || melt_magic_discr ((melt_ptr_t) bigv) != MELTOBMAG_MIXBIGINT)
    goto end;
  {
    mpz_t mp;
    mpz_init (mp);
    if (melt_fill_mpz_from_mixbigint((melt_ptr_t) bigv, mp))
      meltgc_ppout_mpz ((melt_ptr_t) outv, indentsp, mp);
    mpz_clear (mp);
  }
end:
  MELT_EXITFRAME ();
#undef sbufv
#undef bigv
}

/* make a new boxed file */
melt_ptr_t
meltgc_new_file (melt_ptr_t discr_p, FILE* fil)
{
  unsigned mag = 0;
  MELT_ENTERFRAME(2, NULL);
#define discrv meltfram__.mcfr_varptr[0]
#define object_discrv ((meltobject_ptr_t)(discrv))
#define resv   meltfram__.mcfr_varptr[1]
#define spec_resv ((struct meltspecial_st*)(resv))
#define spda_resv ((struct meltspecialdata_st*)(resv))
  discrv = (melt_ptr_t) discr_p;
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  mag = object_discrv->meltobj_magic;
  switch (mag)
    {
    case MELTOBMAG_SPECIAL_DATA:
    {
      resv = (melt_ptr_t) meltgc_make_specialdata ((melt_ptr_t) discrv);
      /* first handle rawfile which are special cases of files */
      if (discrv == MELT_PREDEF(DISCR_RAWFILE)
          || melt_is_subclass_of ((meltobject_ptr_t)discrv,
                                  (meltobject_ptr_t)MELT_PREDEF(DISCR_RAWFILE)))
        {
          spda_resv->meltspec_kind = meltpydkind_rawfile;
          spda_resv->meltspec_payload.meltpayload_file1 = fil;
        }
      else if (discrv == MELT_PREDEF(DISCR_FILE)
               || melt_is_subclass_of ((meltobject_ptr_t)discrv,
                                       (meltobject_ptr_t)MELT_PREDEF(DISCR_FILE)))
        {
          spda_resv->meltspec_kind = meltpydkind_file;
          spda_resv->meltspec_payload.meltpayload_file1 = fil;
        }
    }
    break;
    default:
      resv = NULL;
      goto end;
    }
  goto end;
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef resv
#undef spec_resv
#undef spda_resv
}






/* Write a buffer to a file, but take care to not overwrite the file
   if it does not change. */
void
melt_output_strbuf_to_file_no_overwrite (melt_ptr_t sbufv, const char*filnam)
{
  static unsigned cnt;
  char buf[64];
  time_t now = 0;
  char* tempath = NULL;
  char* bakpath = NULL;
  long r = 0;
  FILE *ftemp =  NULL;
  FILE *filold = NULL;
  bool samefc = false;
  if (melt_magic_discr (sbufv) != MELTOBMAG_STRBUF)
    return;
  if (!filnam || !filnam[0])
    return;
  cnt++;
  memset (buf, 0, sizeof(buf));
  time (&now);
  r = (melt_lrand () & 0x3ffffff) ^ ((long)now & (0x7fffffff));
  snprintf (buf, sizeof (buf), "_n%d_p%d_r%lx.tmp", cnt, (int) getpid(), r);
  tempath = concat (filnam, buf, NULL);
  ftemp = fopen(tempath, "w+");
  if (!ftemp)
    melt_fatal_error ("failed to open temporary %s for uniquely [over-]writing %s - %m",
                      tempath, filnam);
  if (fwrite (melt_strbuf_str (sbufv),
              melt_strbuf_usedlength (sbufv),
              1, ftemp) != 1)
    melt_fatal_error ("failed to write into temporary %s for writing %s - %m",
                      tempath, filnam);
  fflush(ftemp);
  filold = fopen(filnam, "r");
  if (!filold)
    {
      fclose(ftemp);
      if (rename (tempath, filnam))
        melt_fatal_error ("failed to rename %s to new %s - %m",
                          tempath, filnam);
      free (tempath), tempath = NULL;
      return;
    }
  samefc = true;
  rewind (ftemp);
  while (samefc)
    {
      int tc = getc (ftemp);
      int oc = getc (filold);
      if (tc == EOF && oc == EOF)
        break;
      if (tc != oc)
        samefc = false;
    };
  samefc = samefc && feof(ftemp) && feof(filold);
  fclose (ftemp), ftemp = NULL;
  fclose (filold), filold = NULL;
  if (samefc)
    {
      remove (tempath);
      return;
    }
  else
    {
      bakpath = concat (filnam, "~", NULL);
      (void) rename (filnam, bakpath);
      if (rename (tempath, filnam))
        melt_fatal_error ("failed to rename %s to overwritten %s - %m",
                          tempath, filnam);
    }
  free (tempath), tempath = NULL;
  free (bakpath), bakpath = NULL;
}



/***********************************************************
 * generate C++ code for a melt unit name; take care to avoid touching
 * the generated C++ file when it happens to be the same as what existed
 * on disk before, to help the "make" utility.
 ***********************************************************/
void
melt_output_cfile_decl_impl_secondary_option (melt_ptr_t unitnam,
    melt_ptr_t declbuf,
    melt_ptr_t implbuf,
    melt_ptr_t optbuf,
    int filrank)
{
  static unsigned cnt;
  bool samefil = false;
  char *dotcnam = NULL;
  char *dotempnam = NULL;
  char *dotcpercentnam = NULL;
  FILE *cfil = NULL;
  FILE *oldfil = NULL;
  char *mycwd = getpwd ();
  const char *workdir = NULL;
  gcc_assert (melt_magic_discr (unitnam) == MELTOBMAG_STRING);
  gcc_assert (melt_magic_discr (declbuf) == MELTOBMAG_STRBUF);
  gcc_assert (melt_magic_discr (implbuf) == MELTOBMAG_STRBUF);
  cnt++;
  /** FIXME : should implement some policy about the location of the
      generated C file; currently using the pwd */
  {
    const char *s = melt_string_str (unitnam);
    int slen = strlen (s);
    char bufpid[48];
    time_t now = 0;
    time (&now);
    if (melt_flag_generate_work_link)
      workdir = melt_argument ("workdir");
    melt_debugeprintf ("melt_output_cfile_decl_impl_secondary s=%s cnt=%u workdir=%s", s, cnt, workdir);
    /* generate in bufpid a unique file suffix from the pid and the time */
    memset (bufpid, 0, sizeof(bufpid));
    snprintf (bufpid, sizeof(bufpid)-1, "_p%d_n%d_r%x_c%u",
              (int) getpid(), (int) (now%100000), (int)((melt_lrand()) & 0xffff), cnt);
    if (slen>3 && (s[slen-3]!='.' || s[slen-2]!='c' || s[slen-1]!='c'))
      {
        dotcnam = concat (s, ".cc", NULL);
        dotcpercentnam = concat (s, ".cc%", NULL);
        if (workdir && !strcmp(workdir, ".") && melt_flag_generate_work_link)
          dotempnam = concat (workdir, /*DIR_SEPARATOR here*/ "/",  melt_basename (s),
                              ".cc%", bufpid, NULL);
        else
          dotempnam = concat (s, ".cc%", bufpid, NULL);
      }
    else
      {
        dotcnam = xstrdup (s);
        dotcpercentnam = concat (s, "%", NULL);
        if (workdir && !strcmp(workdir, ".") && melt_flag_generate_work_link)
          dotempnam = concat (workdir, /*DIR_SEPARATOR here*/ "/",  melt_basename (s),
                              "%", bufpid, NULL);
        else
          dotempnam = concat (s, "%", bufpid, NULL);
      };
  }
  melt_debugeprintf ("melt_output_cfile_decl_impl_secondary_option dotempnam=%s melt_flag_generate_work_link=%d",
                     dotempnam, melt_flag_generate_work_link);
  /* we first write in the temporary name */
  cfil = fopen (dotempnam, "w");
  if (!cfil)
    melt_fatal_error ("failed to open MELT generated file %s - %m", dotempnam);
  fprintf (cfil,
           "/* GCC MELT GENERATED C++ FILE %s - DO NOT EDIT - see http://gcc-melt.org/ */\n",
           melt_basename (dotcnam));
  if (filrank <= 0)
    {
      if (melt_magic_discr (optbuf) == MELTOBMAG_STRBUF)
        {
          fprintf (cfil, "\n/***+ %s options +***\n",
                   melt_basename (melt_string_str (unitnam)));
          melt_putstrbuf (cfil, optbuf);
          fprintf (cfil, "\n***- end %s options -***/\n",
                   melt_basename (melt_string_str (unitnam)));
        }
      else
        fprintf (cfil, "\n/***+ %s without options +***/\n",
                 melt_basename (melt_string_str (unitnam)));
    }
  else
    fprintf (cfil, "/* secondary MELT generated C++ file of rank #%d */\n",
             filrank);
  fprintf (cfil, "#include \"melt-run.h\"\n\n");;
  if (filrank <= 0)
    fprintf (cfil, "\n/* used hash from melt-run.h when compiling this file: */\n"
             "MELT_EXTERN const char meltrun_used_md5_melt[] = MELT_RUN_HASHMD5 /* from melt-run.h */;\n\n");
  else
    fprintf (cfil, "\n/* used hash from melt-run.h when compiling this file: */\n"
             "MELT_EXTERN const char meltrun_used_md5_melt_f%d[] = MELT_RUN_HASHMD5 /* from melt-run.h */;\n\n", filrank);

  fprintf (cfil, "\n/**** %s declarations ****/\n",
           melt_basename (melt_string_str (unitnam)));
  melt_putstrbuf (cfil, declbuf);
  putc ('\n', cfil);
  fflush (cfil);
  fprintf (cfil, "\n/**** %s implementations ****/\n",
           melt_basename (melt_string_str (unitnam)));
  melt_putstrbuf (cfil, implbuf);
  putc ('\n', cfil);
  fflush (cfil);
  fprintf (cfil, "\n/**** end of %s ****/\n",
           melt_basename (melt_string_str (unitnam)));
  fclose (cfil);
  cfil = 0;
  /* reopen the dotempnam and the dotcnam files to compare their content */
  cfil = fopen (dotempnam, "r");
  if (!cfil)
    melt_fatal_error ("failed to re-open melt generated file %s - %m", dotempnam);
  oldfil = fopen (dotcnam, "r");
  /* we compare oldfil & cfil; if they are the same we don't overwrite
     the oldfil; this is for the happiness of make utility. */
  samefil = oldfil != NULL;
  if (samefil)
    {
      /* files of different sizes are different */
      struct stat cfilstat, oldfilstat;
      memset (&cfilstat, 0, sizeof (cfilstat));
      memset (&oldfilstat, 0, sizeof (oldfilstat));
      if (fstat (fileno(cfil), &cfilstat)
          || fstat (fileno (oldfil), &oldfilstat)
          || cfilstat.st_size != oldfilstat.st_size)
        samefil = false;
    }
  while (samefil && (!feof(cfil) || !feof(oldfil)))
    {
      int c = getc (cfil);
      int o = getc (oldfil);
      if (c != o)
        samefil = false;
      if (c < 0)
        break;
    };
  samefil = samefil && feof(cfil) && feof(oldfil);
  fclose (cfil);
  if (oldfil) fclose (oldfil);
  if (samefil)
    {
      /* Rare case when the generated file is the same as what existed
      in the filesystem, so discard the generated temporary file. */
      if (remove (dotempnam))
        melt_fatal_error ("failed to remove %s as melt generated file - %m",
                          dotempnam);
      if (IS_ABSOLUTE_PATH(dotcnam))
        inform (UNKNOWN_LOCATION, "MELT generated same file %s", dotcnam);
      else
        inform (UNKNOWN_LOCATION, "MELT generated same file %s in %s",
                dotcnam, mycwd);
      if (melt_generated_c_files_list_fil)
        fprintf (melt_generated_c_files_list_fil,
                 "= %s\n", dotcnam);
    }
  else
    {
      bool samemdfil = false;
      char *md5nam = NULL;
      /* Usual case when the generated file is not the same as its
      former variant; rename the old foo.c as foo.c% for backup, etc... */
      (void) rename (dotcnam, dotcpercentnam);

      if (melt_flag_generate_work_link && workdir)
        {
          /* if symlinks to work files are required, we generate a
             unique filename in the work directory using the md5sum of
             the generated file, then we symlink it.. */
          int  mln = 0;
          FILE* mdfil = NULL;
          char *realworkdir = NULL;
          char md5hexbuf[40]; /* larger than md5 hex, for null termination... */
          memset (md5hexbuf, 0, sizeof(md5hexbuf));
          melt_string_hex_md5sum_file_to_hexbuf (dotempnam, md5hexbuf);
          if (!md5hexbuf[0])
            melt_fatal_error ("failed to compute md5sum of %s - %m", dotempnam);
          realworkdir = lrealpath (workdir);
          md5nam = concat (realworkdir, /*DIR_SEPARATOR here*/ "/",
                           melt_basename (dotcnam), NULL);
          free (realworkdir), realworkdir = NULL;
          mln = strlen (md5nam);
          if (mln>4 && !strcmp(md5nam + mln -3, ".cc"))
            md5nam[mln-2] = (char)0;
          md5nam = reconcat (md5nam, md5nam, ".", md5hexbuf, ".mdsumed.cc", NULL);
          mdfil = fopen (md5nam, "r");
          if (mdfil)
            {
              FILE* dotempfil = fopen(dotempnam, "r");
              samemdfil = (dotempfil != NULL);
              while (samemdfil && (!feof(mdfil) || !feof(dotempfil)))
                {
                  int c = getc (mdfil);
                  int o = getc (dotempfil);
                  if (c != o)
                    samemdfil = false;
                  if (c < 0) /*both are eof*/
                    break;
                };
              fclose (mdfil), mdfil = NULL;
              if (dotempfil)
                fclose (dotempfil);
            };
          if (samemdfil)
            {
              (void) remove (dotempnam);
              if (symlink (md5nam, dotcnam))
                melt_fatal_error ("failed to symlink old %s as %s - %m", md5nam, dotcnam);
              if (IS_ABSOLUTE_PATH (dotcnam))
                inform (UNKNOWN_LOCATION, "MELT symlinked existing file %s to %s",
                        md5nam, dotcnam);
              else
                inform (UNKNOWN_LOCATION, "MELT symlinked existing file %s to %s in %s",
                        md5nam, dotcnam, mycwd);
              if (melt_generated_c_files_list_fil)
                {
                  fprintf (melt_generated_c_files_list_fil,
                           "# same symlink -> %s:\n", md5nam);
                  fprintf (melt_generated_c_files_list_fil, "= %s\n", dotcnam);
                }
            }
          else
            {
              /* if the file md5nam exist, either we have an improbable md5
              collision, or it was edited after generation. */
              if (!access (md5nam, R_OK))
                inform (UNKNOWN_LOCATION,
                        "MELT overwriting generated file %s (perhaps manually edited)", md5nam);
              if (rename (dotempnam, md5nam))
                melt_fatal_error ("failed to rename %s as %s melt generated work file - %m",
                                  dotempnam, md5nam);
              if (symlink (md5nam, dotcnam))
                melt_fatal_error ("failed to symlink new %s as %s - %m", md5nam, dotcnam);
              if (IS_ABSOLUTE_PATH (dotcnam))
                inform (UNKNOWN_LOCATION, "MELT symlinked new file %s to %s",
                        md5nam, dotcnam);
              else
                inform (UNKNOWN_LOCATION, "MELT symlinked new file %s to %s in %s",
                        md5nam, dotcnam, mycwd);
              if (melt_generated_c_files_list_fil)
                fprintf (melt_generated_c_files_list_fil,
                         "# symlink to new %s is:\n"
                         "+ %s\n", md5nam, dotcnam);
            }
          free (md5nam), md5nam = NULL;
        }
      else
        {
          /* rename the generated temporary */
          if (rename (dotempnam, dotcnam))
            melt_fatal_error ("failed to rename %s as %s melt generated file - %m",
                              dotempnam, dotcnam);
          if (IS_ABSOLUTE_PATH (dotcnam))
            inform (UNKNOWN_LOCATION, "MELT generated new file %s", dotcnam);
          else
            inform (UNKNOWN_LOCATION, "MELT generated new file %s in %s",
                    dotcnam, mycwd);
          if (melt_generated_c_files_list_fil)
            fprintf (melt_generated_c_files_list_fil,
                     "#new file:\n"
                     "+ %s\n",
                     dotcnam);
        }
    }
  melt_debugeprintf ("output_cfile done dotcnam %s", dotcnam);
  free (dotcnam);
  free (dotempnam);
  free (dotcpercentnam);
}




/* recursive function to output to a file. Handle boxed integers,
   lists, tuples, strings, strbufs, but don't handle objects! */
void meltgc_output_file (FILE* fil, melt_ptr_t val_p)
{
  MELT_ENTERFRAME(4, NULL);
#define valv        meltfram__.mcfr_varptr[0]
#define compv       meltfram__.mcfr_varptr[1]
#define pairv       meltfram__.mcfr_varptr[2]
  valv = val_p;
  if (!fil || !valv) goto end;
  switch (melt_magic_discr((melt_ptr_t)valv))
    {
    case MELTOBMAG_STRING:
      melt_puts (fil, melt_string_str ((melt_ptr_t)valv));
      break;
    case MELTOBMAG_STRBUF:
      melt_puts (fil, melt_strbuf_str ((melt_ptr_t)valv));
      break;
    case MELTOBMAG_INT:
      fprintf (fil, "%ld", melt_get_int ((melt_ptr_t)valv));
      break;
    case MELTOBMAG_LIST:
    {
      for (pairv = (melt_ptr_t) ((struct meltlist_st*)(valv))->first;
           pairv && melt_magic_discr((melt_ptr_t)pairv) == MELTOBMAG_PAIR;
           pairv = (melt_ptr_t) ((struct meltpair_st*)(pairv))->tl)
        {
          compv = ((struct meltpair_st*)(pairv))->hd;
          if (compv)
            meltgc_output_file (fil, (melt_ptr_t) compv);
          compv = NULL;
        };
      pairv = NULL;   /* for GC happiness */
    }
    break;
    case MELTOBMAG_MULTIPLE:
    {
      int sz = ((struct meltmultiple_st*)(valv))->nbval;
      int ix = 0;
      for (ix = 0; ix < sz; ix ++)
        {
          compv = melt_multiple_nth ((melt_ptr_t)valv, ix);
          if (!compv)
            continue;
          meltgc_output_file (fil, (melt_ptr_t) compv);
        }
    }
    break;
    default:
      /* FIXME: perhaps add a warning, or handle more cases... */
      ;
    }
end:
  MELT_EXITFRAME();
#undef valv
#undef compv
#undef pairv
}

/* Added */
#undef melt_assert_failed
#undef melt_check_failed

void
melt_assert_failed (const char *msg, const char *filnam,
                    int lineno, const char *fun)
{
  time_t nowt = 0;
  static char msgbuf[600];
  if (!msg)
    msg = "??no-msg??";
  if (!filnam)
    filnam = "??no-filnam??";
  if (!fun)
    fun = "??no-func??";
  if (melt_dbgcounter > 0)
    snprintf (msgbuf, sizeof (msgbuf) - 1,
              "%s:%d: MELT ASSERT #!%ld: %s {%s}", melt_basename (filnam),
              lineno, melt_dbgcounter, fun, msg);
  else
    snprintf (msgbuf, sizeof (msgbuf) - 1, "%s:%d: MELT ASSERT: %s {%s}",
              melt_basename (filnam), lineno, fun, msg);
  time (&nowt);
  melt_fatal_info (filnam, lineno);
  /* don't call melt_fatal_error here! */
  if (melt_dbgcounter > 0)
    fatal_error (UNKNOWN_LOCATION, "%s:%d: MELT ASSERT FAILED <%s> : %s\n  [dbg#%ld] @ %s\n",
                 melt_basename (filnam), lineno, fun, msg, melt_dbgcounter, ctime (&nowt));
  else
    fatal_error (UNKNOWN_LOCATION, "%s:%d: MELT ASSERT FAILED <%s> : %s\n @ %s\n",
                 melt_basename (filnam), lineno, fun, msg, ctime (&nowt));
}


/* Should usually be called from melt_fatal_error macro... */
void
melt_fatal_info (const char*filename, int lineno)
{
  int ix = 0;
  const char* workdir = NULL;
  int workdirlen = 0;
  if (filename != NULL && lineno>0)
    {
      error ("MELT fatal failure from %s:%d [MELT built %s, version %s]",
             filename, lineno, melt_runtime_build_date, melt_version_str ());
      inform (UNKNOWN_LOCATION,
              "MELT failed at %s:%d in directory %s", filename, lineno,
              getpwd ());
    }
  else
    {
      error ("MELT fatal failure without location [MELT built %s, version %s]",
             melt_runtime_build_date, melt_version_str ());
      inform (UNKNOWN_LOCATION,
              "MELT failed in directory %s", getpwd ());
    }
  workdir = melt_argument("workdir");
  if (workdir && workdir[0])
    {
      workdirlen = (int) strlen(workdir);
      inform (UNKNOWN_LOCATION,
              "MELT failed with work directory %s", workdir);
    }
  fflush (NULL);
  if (MELT_HAVE_RUNTIME_DEBUG > 0 || melt_flag_debug > 0)
    melt_dbgshortbacktrace ("MELT fatal failure", 100);
  /* Index 0 is unused in melt_modulinfo. */
  for (ix = 1; ix <= Melt_Module::nb_modules(); ix++)
    {
      Melt_Module* cmod = Melt_Module::nth_module(ix);
      const char*curmodpath = NULL;
      const char*curmodgen = NULL;
      if (!cmod)
        continue;
      gcc_assert (cmod->valid_magic());
      curmodpath = cmod->module_path();
      curmodgen = static_cast<const char*>(cmod->get_dlsym("melt_gen_timestamp"));
      if (curmodgen && curmodgen[0])
        {
          if (workdirlen>0 && !strncmp (workdir, curmodpath, workdirlen))
            inform (UNKNOWN_LOCATION,
                    "MELT failure with loaded work module #%d: %s generated on %s",
                    ix, curmodpath+workdirlen, curmodgen);
          else
            inform (UNKNOWN_LOCATION,
                    "MELT failure with loaded module #%d: %s generated on %s",
                    ix, melt_basename (curmodpath), curmodgen);
        }
      else
        {
          if (workdirlen>0 && !strncmp (workdir, curmodpath, workdirlen))
            inform (UNKNOWN_LOCATION,
                    "MELT failure with loaded work module #%d: %s",
                    ix, curmodpath+workdirlen);
          else
            inform (UNKNOWN_LOCATION,
                    "MELT failure with loaded module #%d: %s",
                    ix, melt_basename (curmodpath));
        }
    };
  if (filename != NULL && lineno>0)
    inform (UNKNOWN_LOCATION,
            "MELT got fatal failure from %s:%d", filename, lineno);
  if (cfun && cfun->decl)
    inform (UNKNOWN_LOCATION,
            "MELT got fatal failure with current function (cfun %p) as %q+D",
            (void*) cfun, cfun->decl);
  if (current_pass)
    inform (UNKNOWN_LOCATION,
            "MELT got fatal failure from current_pass %p #%d named %s",
            (void*) current_pass,
            current_pass->static_pass_number, current_pass->name);
  if (melt_tempdir[0])
    warning (0, "MELT temporary directory %s may be dirty on fatal failure; please remove it manually",
             melt_tempdir);
  fflush (NULL);
  melt_debugeprintf ("ending melt_fatal_info filename=%s lineno=%d\n", filename, lineno);
  return;
}



void
melt_check_failed (const char *msg, const char *filnam,
                   int lineno, const char *fun)
{
  static char msgbuf[500];
  if (!msg)
    msg = "??no-msg??";
  if (!filnam)
    filnam = "??no-filnam??";
  if (!fun)
    fun = "??no-func??";
  if (melt_dbgcounter > 0)
    snprintf (msgbuf, sizeof (msgbuf) - 1,
              "%s:%d: MELT CHECK #!%ld: %s {%s}", melt_basename (filnam),
              lineno, melt_dbgcounter, fun, msg);
  else
    snprintf (msgbuf, sizeof (msgbuf) - 1, "%s:%d: MELT CHECK: %s {%s}",
              melt_basename (filnam), lineno, fun, msg);
  melt_dbgshortbacktrace (msgbuf, 100);
  warning (0, "%s:%d: MELT CHECK FAILED <%s> : %s\n",
           melt_basename (filnam), lineno, fun, msg);
}




/* convert a MELT value to a plugin flag or option */
unsigned long
melt_val2passflag(melt_ptr_t val_p)
{
  unsigned long res = 0;
  int valmag = 0;
  MELT_ENTERFRAME (3, NULL);
#define valv    meltfram__.mcfr_varptr[0]
#define compv   meltfram__.mcfr_varptr[1]
#define pairv   meltfram__.mcfr_varptr[2]
  valv = val_p;
  if (!valv) goto end;
  valmag = melt_magic_discr((melt_ptr_t) valv);
  if (valmag == MELTOBMAG_INT || valmag == MELTOBMAG_MIXINT)
    {
      res = melt_get_int((melt_ptr_t) valv);
      goto end;
    }
  else if (valmag == MELTOBMAG_OBJECT
           && melt_is_instance_of((melt_ptr_t) valv,
                                  (melt_ptr_t) MELT_PREDEF(CLASS_NAMED)))
    {
      compv = ((meltobject_ptr_t)valv)->obj_vartab[MELTFIELD_NAMED_NAME];
      res = melt_val2passflag((melt_ptr_t) compv);
      goto end;
    }
  else if (valmag == MELTOBMAG_STRING)
    {
      const char *valstr = melt_string_str((melt_ptr_t) valv);
      /* should be kept in sync with the defines in tree-pass.h */
#define WHENFLAG(F) if (!strcasecmp(valstr, #F)) { res = F; goto end; }
      WHENFLAG(PROP_gimple_any);
      WHENFLAG(PROP_gimple_lcf);
      WHENFLAG(PROP_gimple_leh);
      WHENFLAG(PROP_cfg);
      WHENFLAG(PROP_ssa);
      WHENFLAG(PROP_no_crit_edges);
      WHENFLAG(PROP_rtl);
      WHENFLAG(PROP_gimple_lomp);
      WHENFLAG(PROP_cfglayout);
      WHENFLAG(PROP_trees);
      /* likewise for TODO flags */
      WHENFLAG(TODO_do_not_ggc_collect);
      WHENFLAG(TODO_cleanup_cfg);
      WHENFLAG(TODO_dump_symtab);
      WHENFLAG(TODO_remove_functions);
      WHENFLAG(TODO_rebuild_frequencies);
      WHENFLAG(TODO_update_ssa);
      WHENFLAG(TODO_update_ssa_no_phi);
      WHENFLAG(TODO_update_ssa_full_phi);
      WHENFLAG(TODO_update_ssa_only_virtuals);
      WHENFLAG(TODO_remove_unused_locals);
      WHENFLAG(TODO_df_finish);
      WHENFLAG(TODO_df_verify);
      WHENFLAG(TODO_mark_first_instance);
      WHENFLAG(TODO_rebuild_alias);
      WHENFLAG(TODO_update_address_taken);
      WHENFLAG(TODO_update_ssa_any);
      WHENFLAG(TODO_verify_all);
      WHENFLAG(TODO_verify_il);
#undef WHENFLAG
      goto end;
    }
  else if (valmag == MELTOBMAG_LIST)
    {
      for (pairv = (melt_ptr_t) ((struct meltlist_st *) valv)->first;
           melt_magic_discr ((melt_ptr_t) pairv) ==
           MELTOBMAG_PAIR;
           pairv = (melt_ptr_t) ((struct meltpair_st *)pairv)->tl)
        {
          compv = ((struct meltpair_st *)pairv)->hd;
          res |= melt_val2passflag((melt_ptr_t) compv);
        }
    }
  else if (valmag == MELTOBMAG_MULTIPLE)
    {
      int i=0, l=0;
      l = melt_multiple_length((melt_ptr_t)valv);
      for (i=0; i<l; i++)
        {
          compv = melt_multiple_nth((melt_ptr_t) valv, i);
          res |= melt_val2passflag((melt_ptr_t) compv);
        }
    }
end:
  MELT_EXITFRAME();
  return res;
#undef valv
#undef compv
#undef pairv
}


FILE*
meltgc_set_dump_file (FILE* dumpf)
{
  FILE *oldf = NULL;
  MELT_ENTERFRAME(1, NULL);
#define dumpv        meltfram__.mcfr_varptr[0]
  dumpv = melt_get_inisysdata (MELTFIELD_SYSDATA_DUMPFILE);
  if (melt_discr((melt_ptr_t) dumpv) == (meltobject_ptr_t) MELT_PREDEF(DISCR_RAWFILE))
    {
      if (melt_magic_discr ((melt_ptr_t) dumpv) == MELTOBMAG_SPECIAL_DATA
          && ((struct meltspecialdata_st*)dumpv)->meltspec_kind == meltpydkind_rawfile)
        {
          oldf = ((struct meltspecialdata_st*)dumpv)->meltspec_payload.meltpayload_file1;
          if (oldf)
            fflush (oldf);
          ((struct meltspecialdata_st*)dumpv)->meltspec_payload.meltpayload_file1 = dumpf;
          goto end;
        }
    }
end:
  MELT_EXITFRAME();
  return oldf;
#undef dumpv
}


void
meltgc_restore_dump_file (FILE* oldf)
{
  MELT_ENTERFRAME(1, NULL);
#define dumpv        meltfram__.mcfr_varptr[0]
  if (dump_file)
    fflush (dump_file);
  dumpv = melt_get_inisysdata (MELTFIELD_SYSDATA_DUMPFILE);
  if (melt_discr((melt_ptr_t) dumpv) == (meltobject_ptr_t) MELT_PREDEF(DISCR_RAWFILE))
    {
      if (melt_magic_discr ((melt_ptr_t) dumpv) == MELTOBMAG_SPECIAL_DATA
          && ((struct meltspecialdata_st*)dumpv)->meltspec_kind == meltpydkind_rawfile)
        {
          ((struct meltspecialdata_st*)dumpv)->meltspec_payload.meltpayload_file1 = oldf;
          goto end;
        }
    }
end:
  MELT_EXITFRAME();
#undef dumpv
}








#if MELT_HAVE_DEBUG
/* some useless routines in wich we can add a breakpoint from gdb. */
void
melt_sparebreakpoint_0_at (const char*fil, int lin, void*ptr, const char*msg)
{
  dbgprintf_raw ("@%s:%d: MELT sparebreakpoint_0 ptr=%p msg=%s\n",
                 fil, lin, ptr, msg);
  char msgbuf[128];
  snprintf (msgbuf, sizeof(msgbuf), "melt_sparebreakpoint_0@%.25s:%d: %s",
            melt_basename (fil), lin, msg);
  melt_dbgshortbacktrace(msgbuf, 20);
  melt_debugeprintf ("melt_sparebreakpoint_0_at msg %s", msg);
  gcc_assert (fil != NULL); // useless, but can put a GDB breakpoint here
}

void
melt_sparebreakpoint_1_at (const char*fil, int lin, void*ptr, const char*msg)
{
  dbgprintf_raw ("@%s:%d: MELT sparebreakpoint_1 ptr=%p msg=%s\n",
                 fil, lin, ptr, msg);
  char msgbuf[128];
  snprintf (msgbuf, sizeof(msgbuf), "melt_sparebreakpoint_1@%.25s:%d: %s",
            melt_basename (fil), lin, msg);
  melt_dbgshortbacktrace(msgbuf, 20);
  melt_debugeprintf ("melt_sparebreakpoint_1_at msg %s", msg);
  gcc_assert (fil != NULL); // useless, but can put a GDB breakpoint here
}

void
melt_sparebreakpoint_2_at (const char*fil, int lin, void*ptr, const char*msg)
{
  dbgprintf_raw ("@%s:%d: MELT sparebreakpoint_2 ptr=%p msg=%s\n",
                 fil, lin, ptr, msg);
  char msgbuf[128];
  snprintf (msgbuf, sizeof(msgbuf), "melt_sparebreakpoint_2@%.25s:%d: %s",
            melt_basename (fil), lin, msg);
  melt_dbgshortbacktrace(msgbuf, 20);
  melt_debugeprintf ("melt_sparebreakpoint_2_at msg %s", msg);
  gcc_assert (fil != NULL); // useless, but can put a GDB breakpoint here
}

/* To be called from the gdb debugger only */
MELT_EXTERN void melt_low_debug_for_gdb(const char*msg, melt_ptr_t val);

void melt_low_debug_for_gdb(const char*msg, melt_ptr_t val)
{
  melt_low_debug_value_at("*fromgdb*", 0, msg, val);
}


#endif /*MELT_HAVE_DEBUG*/





/* This meltgc_handle_signal routine is called thru the
   MELT_CHECK_SIGNAL macro, which is generated in many places in C
   code generated from MELT.  The MELT_CHECK_SIGNAL macro is
   testing the volatile melt_signaled flag before calling this.
   Raw signal handlers (e.g. melt_raw_sigio_signal or
   melt_raw_sigalrm_signal) should set that flag (with perhaps
   others). */
void
melt_handle_signal (void)
{
  melt_signaled = 0;
  if (melt_got_sigio)
    {
      melt_got_sigio = 0;
      melthookproc_HOOK_HANDLE_SIGIO ();
    }
  if (melt_got_sigalrm)
    {
      melt_got_sigalrm = 0;
      melthookproc_HOOK_HANDLE_SIGALRM ();
    }
  if (melt_got_sigchld)
    {
      melt_got_sigchld = 0;
      melthookproc_HOOK_HANDLE_SIGCHLD ();
    }
}



/* allocate e new empty longsbucket */
melt_ptr_t
meltgc_new_longsbucket (meltobject_ptr_t discr_p,
                        unsigned len)
{
  unsigned lenix = 0;
  unsigned bucklen = 0;
  MELT_ENTERFRAME (2, NULL);
#define discrv       meltfram__.mcfr_varptr[0]
#define buckv        meltfram__.mcfr_varptr[1]
  discrv = (melt_ptr_t) discr_p;
  MELT_LOCATION_HERE ("meltgc_new_longsbucket");
  if (!discrv)
    discrv = MELT_PREDEF (DISCR_BUCKET_LONGS);
  if (melt_magic_discr ((melt_ptr_t) (discrv)) != MELTOBMAG_OBJECT)
    goto end;
  if (((meltobject_ptr_t) (discrv))->meltobj_magic != MELTOBMAG_BUCKETLONGS)
    goto end;
  len += len/16 + 4;
  for (lenix = 2;
       (bucklen = melt_primtab[lenix]) != 0 && bucklen < len;
       lenix++)
    (void)0;
  if (bucklen == 0)
    melt_fatal_error("meltgc_new_longsbucket: too big bucket length %u",
                     len);
  gcc_assert (lenix>0);
  buckv = (melt_ptr_t)
          meltgc_allocate (sizeof (struct meltbucketlongs_st),
                           sizeof (struct melt_bucketlongentry_st)*bucklen);
  ((struct meltbucketlongs_st*)(buckv))->discr = (meltobject_ptr_t) discrv;
  ((struct meltbucketlongs_st*)(buckv))->buckl_aux = NULL;
  ((struct meltbucketlongs_st*)(buckv))->buckl_lenix = lenix;
  ((struct meltbucketlongs_st*)(buckv))->buckl_xnum = 0;
  ((struct meltbucketlongs_st*)(buckv))->buckl_ucount = 0;
  memset (((struct meltbucketlongs_st*)(buckv))->buckl_entab,
          0,
          bucklen*sizeof(struct melt_bucketlongentry_st));
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) buckv;
#undef buckv
#undef valv
}



/* replace the value associated in a bucket of longs to a long key;
   don't do anything if the key was absent; return the old value
   associated to that key, or else NULL. */
melt_ptr_t
meltgc_longsbucket_replace (melt_ptr_t bucket_p, long key, melt_ptr_t val_p)
{
  struct meltbucketlongs_st*buck = NULL;
  unsigned len = 0;
  unsigned lo=0, hi=0, md=0, ucnt=0;
  MELT_ENTERFRAME (3, NULL);
#define buckv        meltfram__.mcfr_varptr[0]
#define valv         meltfram__.mcfr_varptr[1]
#define resv         meltfram__.mcfr_varptr[2]
  buckv = bucket_p;
  valv = val_p;
  if (melt_magic_discr ((melt_ptr_t) buckv) != MELTOBMAG_BUCKETLONGS || !valv)
    goto end;
  buck = (struct meltbucketlongs_st*)(buckv);
  len = melt_primtab[buck->buckl_lenix];
  ucnt = buck->buckl_ucount;
  gcc_assert (ucnt <= len);
  if (ucnt == 0)
    goto end;
  lo = 0;
  hi = ucnt - 1;
  while (lo + 2 < hi)
    {
      long curk = 0;
      md = (lo + hi) / 2;
      curk = buck->buckl_entab[md].ebl_at;
      if (curk < key)
        lo = md;
      else
        hi = md;
    };
  for (md = lo; md <= hi; md++)
    if (buck->buckl_entab[md].ebl_at == key)
      {
        resv = buck->buckl_entab[md].ebl_va;
        buck->buckl_entab[md].ebl_va = (melt_ptr_t) valv;
        meltgc_touch_dest ((melt_ptr_t)buckv,  (melt_ptr_t)valv);
        goto end;
      }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef buckv
#undef bu_buckv
#undef valv
#undef resv
}




/* put or replace the value associated in a bucket of longs; return
   the re-allocated bucket or the same one, or else NULL */
melt_ptr_t
meltgc_longsbucket_put (melt_ptr_t bucket_p, long key, melt_ptr_t val_p)
{
  struct meltbucketlongs_st*buck = NULL;
  int len = 0;
  int lo=0, hi=0, md=0, ucnt=0;
  MELT_ENTERFRAME (3, NULL);
#define buckv        meltfram__.mcfr_varptr[0]
#define valv         meltfram__.mcfr_varptr[1]
#define resv         meltfram__.mcfr_varptr[2]
  buckv = bucket_p;
  valv = val_p;
  MELT_LOCATION_HERE ("meltgc_longsbucket_put");
  resv = NULL;
  if (melt_magic_discr ((melt_ptr_t) buckv) != MELTOBMAG_BUCKETLONGS || !valv)
    goto end;
  buck = (struct meltbucketlongs_st*)(buckv);
  len = melt_primtab[buck->buckl_lenix];
  ucnt = buck->buckl_ucount;
  gcc_assert (ucnt <= len && len > 0);
  if (ucnt + 1 >= len)
    {
      /*  buck is nearly full, allocate a bigger one. */
      struct meltbucketlongs_st*oldbuck = NULL;
      unsigned newcnt = 0;
      int ix = 0;
      bool need_insert = true;
      MELT_LOCATION_HERE ("meltgc_longsbucket_put growing");
      resv = meltgc_new_longsbucket (buck->discr, ucnt + ucnt/5 + 8);
      /* set again buck, because a GC could have occurred */
      oldbuck = (struct meltbucketlongs_st*)(buckv);
      buck = (struct meltbucketlongs_st*)(resv);
      buck->buckl_aux = oldbuck->buckl_aux;
      buck->buckl_xnum = oldbuck->buckl_xnum;
      for (ix = 0; ix < ucnt; ix++)
        {
          long oldkey = oldbuck->buckl_entab[ix].ebl_at;
          if (oldkey < key)
            {
              buck->buckl_entab[newcnt] = oldbuck->buckl_entab[ix];
              newcnt++;
            }
          else if (oldkey == key)
            {
              buck->buckl_entab[newcnt].ebl_at = key;
              buck->buckl_entab[newcnt].ebl_va = (melt_ptr_t) valv;
              need_insert = false;
              newcnt ++;
            }
          else        /* oldkey > key */
            {
              if (need_insert)
                {
                  buck->buckl_entab[newcnt].ebl_at = key;
                  buck->buckl_entab[newcnt].ebl_va = (melt_ptr_t) valv;
                  need_insert = false;
                  newcnt ++;
                };
              buck->buckl_entab[newcnt] = oldbuck->buckl_entab[ix];
              newcnt++;
            }
        };
      if (need_insert)
        {
          buck->buckl_entab[newcnt].ebl_at = key;
          buck->buckl_entab[newcnt].ebl_va = (melt_ptr_t) valv;
          need_insert = false;
          newcnt ++;
        };
      buck->buckl_ucount = newcnt;
      gcc_assert (newcnt >= (unsigned) ucnt && newcnt < melt_primtab[buck->buckl_lenix]);
      meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) valv);
    }
  else if (ucnt == 0)
    {
      /* buck is empty, add first slot & keep it. */
      resv = buckv;
      buck->buckl_entab[0].ebl_at = key;
      buck->buckl_entab[0].ebl_va = (melt_ptr_t) valv;
      buck->buckl_ucount = 1;
      meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) valv);
    }
  else
    {
      /* buck is not full and non empty, keep it. */
      resv = buckv;
      lo = 0;
      hi = ucnt - 1;
      while (lo + 2 < hi)
        {
          long curk = 0;
          md = (lo + hi) / 2;
          curk = buck->buckl_entab[md].ebl_at;
          if (curk < key)
            lo = md;
          else
            hi = md;
        };
      for (md = lo; md <= hi; md++)
        {
          long curk = 0;
          curk = buck->buckl_entab[md].ebl_at;
          if (curk < key)
            continue;
          else if (curk == key)
            {
              buck->buckl_entab[md].ebl_va = (melt_ptr_t) valv;
              meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) valv);
              goto end;
            }
          else
            {
              /* curk > key, so insert here by moving
                       further slots downwards. */
              int ix;
              for (ix = (int)ucnt; ix >= (int)md; ix--)
                buck->buckl_entab[ix+1] = buck->buckl_entab[ix];
              buck->buckl_entab[md].ebl_at = key;
              buck->buckl_entab[md].ebl_va = (melt_ptr_t) valv;
              buck->buckl_ucount = ucnt+1;
              meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) valv);
              goto end;
            }
        };
      if (buck->buckl_entab[ucnt-1].ebl_at < key)
        {
          /* append new slot at end */
          buck->buckl_entab[ucnt].ebl_at = key;
          buck->buckl_entab[ucnt].ebl_va = (melt_ptr_t) valv;
          buck->buckl_ucount = ucnt+1;
          meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) valv);
          goto end;
        }
    }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef buckv
#undef valv
#undef resv
}


/* Remove the value associated in a bucket of longs; return the
   shrinked bucket or the same one, or else NULL */
melt_ptr_t
meltgc_longsbucket_remove (melt_ptr_t bucket_p, long key)
{
  struct meltbucketlongs_st*buck = NULL;
  int len = 0;
  int lo=0, hi=0, md=0, ucnt=0;
  MELT_ENTERFRAME (2, NULL);
#define buckv        meltfram__.mcfr_varptr[0]
#define resv         meltfram__.mcfr_varptr[1]
  buckv = bucket_p;
  resv = NULL;
  MELT_LOCATION_HERE ("meltgc_longsbucket_remove");
  if (melt_magic_discr ((melt_ptr_t) buckv) != MELTOBMAG_BUCKETLONGS)
    goto end;
  buck = (struct meltbucketlongs_st*)(buckv);
  len = melt_primtab[buck->buckl_lenix];
  ucnt = (int) buck->buckl_ucount;
  gcc_assert (ucnt <= len && len > 0);
  if (len > 10 && 2*ucnt + 3<len)   /* shrink the bucket */
    {
      struct meltbucketlongs_st*oldbuck = NULL;
      int ix = 0, newcnt = 0;
      MELT_LOCATION_HERE ("meltgc_longsbucket_remove shrinking");
      resv = meltgc_new_longsbucket (buck->discr, ucnt + 1);
      /* set again buck, because a GC could have occurred */
      oldbuck = (struct meltbucketlongs_st*)(buckv);
      buck = (struct meltbucketlongs_st*)(resv);
      buck->buckl_aux = oldbuck->buckl_aux;
      buck->buckl_xnum = oldbuck->buckl_xnum;
      for (ix = 0; ix < ucnt; ix++)
        {
          long oldkey = oldbuck->buckl_entab[ix].ebl_at;
          if (oldkey == key)
            continue;
          buck->buckl_entab[newcnt] = oldbuck->buckl_entab[ix];
          newcnt++;
        }
      buck->buckl_ucount = newcnt;
    }
  else          /* keep the bucket */
    {
      resv = buckv;
      lo = 0;
      if (ucnt == 0)
        goto end;
      hi = ucnt - 1;
      while (lo + 2 < hi)
        {
          long curk = 0;
          md = (lo + hi) / 2;
          curk = buck->buckl_entab[md].ebl_at;
          if (curk < key)
            lo = md;
          else
            hi = md;
        };
      for (md = lo; md <= hi; md++)
        {
          long curk = 0;
          int ix = 0;
          curk = buck->buckl_entab[md].ebl_at;
          if (curk != key)
            continue;
          for (ix = md+1; ix<ucnt; ix++)
            buck->buckl_entab[ix-1] = buck->buckl_entab[ix];
          buck->buckl_entab[ucnt].ebl_at = 0;
          buck->buckl_entab[ucnt].ebl_va = NULL;
          buck->buckl_ucount = ucnt - 1;
          goto end;
        }
    }
end:
  MELT_EXITFRAME ();
  return (melt_ptr_t) resv;
#undef buckv
#undef valv
#undef resv
}

/* Set the auxiliary data in a longsbucket */
void
meltgc_longsbucket_set_aux (melt_ptr_t bucket_p, melt_ptr_t aux_p)
{
  struct meltbucketlongs_st*buck = NULL;
  MELT_ENTERFRAME (2, NULL);
#define buckv        meltfram__.mcfr_varptr[0]
#define auxv         meltfram__.mcfr_varptr[1]
  buckv = bucket_p;
  auxv = aux_p;
  MELT_LOCATION_HERE ("meltgc_longsbucket_set_aux");
  if (melt_magic_discr ((melt_ptr_t) buckv) != MELTOBMAG_BUCKETLONGS || !auxv)
    goto end;
  buck = (struct meltbucketlongs_st*)(buckv);
  buck->buckl_aux = (melt_ptr_t) auxv;
  meltgc_touch_dest ((melt_ptr_t) buck, (melt_ptr_t) auxv);
end:
  MELT_EXITFRAME ();
#undef buckv
#undef auxv
}

/*****************************************************************/

void melt_set_flag_debug (void)
{
  time_t now;
  melt_flag_debug = 1;
  time (&now);
  melt_debugeprintf(" melt_set_flag_debug  forcibly set debug %s",
                    ctime(&now));
}

void melt_clear_flag_debug (void)
{
  time_t now;
  time (&now);
  melt_debugeprintf(" melt_clear_flag_debug forcibly clear debug %s",
                    ctime(&now));
  melt_flag_debug = 0;
}

/* With GCC 4.8, the gimple_seq are disappearing because they are the
same as gimple (with file "coretypes.h" having the definition `typedef
gimple gimple_seq;`), but our generated runtime support might still
want their old marking routine.  */

#if GCCPLUGIN_VERSION >= 6000
void melt_gt_ggc_mx_gimple_seq_d(void*p)
{
  if (p)
    {
      melt_gimpleseqptr_t gs = reinterpret_cast<melt_gimpleseqptr_t>(p);
      /// gt_ggc_mx_gimple is in generated gtype-desc.c
      gt_ggc_mx_gimple (gs);
    }
}

#elif GCCPLUGIN_VERSION >= 5000
void melt_gt_ggc_mx_gimple_seq_d(void*p)
{
  if (p)
    {
      gimple_seq gs = reinterpret_cast<gimple_seq>(p);
      /// gt_ggc_mx_gimple_statement_base is in generated gtype-desc.h
      gt_ggc_mx_gimple_statement_base (gs);
    }
}
#else
#error melt_gt_ggc_mx_gimple_seq_d unimplemented for this version of GCC
#endif /* GCC 6, 5 or less */


///////////////// always at end of file
/* For debugging purposes, used thru gdb.  */
// for some reason, I need to always declare these, so before any include;
// this might be a dirty hack...
#undef melt_alptr_1
#undef melt_alptr_2
#undef melt_objhash_1
#undef melt_objhash_2
extern "C" {
  void *melt_alptr_1=(void*)0;
  void *melt_alptr_2=(void*)0;
  unsigned melt_objhash_1=0;
  unsigned melt_objhash_2=0;
#if MELT_HAVE_RUNTIME_DEBUG > 0
  const int melt_have_runtime_debug = MELT_HAVE_RUNTIME_DEBUG;
#else
  const int melt_dont_have_runtime_debug =  __LINE__;
#endif
}
/* eof $Id$ */
