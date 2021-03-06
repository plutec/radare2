/* radare - LGPL - Copyright 2009-2017 - pancake */

#include <r_core.h>

#define NODECB(w,x,y) r_config_set_cb (cfg,w,x,y)
#define NODEICB(w,x,y) r_config_set_i_cb (cfg,w,x,y)
#define SETDESC(x,y) r_config_node_desc (x,y)
#define SETOPTIONS(x, ...) set_options (x, __VA_ARGS__)
#define SETI(x,y,z) SETDESC (r_config_set_i (cfg,x,y), z)
#define SETICB(w,x,y,z) SETDESC (NODEICB (w,x,y), z)
#define SETPREF(x,y,z) SETDESC (r_config_set (cfg,x,y), z)
#define SETCB(w,x,y,z) SETDESC (NODECB (w,x,y), z)

static void set_options(RConfigNode *node, ...) {
	va_list argp;
	char *option = NULL;
	va_start (argp, node);
	option = va_arg (argp, char *);
	while (option) {
		r_list_append (node->options, option);
		option = va_arg (argp, char *);
	}
	va_end (argp);
}

static void print_node_options(RConfigNode *node) {
	RListIter *iter;
	char *option;
	r_list_foreach (node->options, iter, option) {
		r_cons_printf ("%s\n", option);
	}
}

/* TODO: use loop here */
/*------------------------------------------------------------------------------------------*/

static int compareName(const RAnalFunction *a, const RAnalFunction *b) {
	return a && b && a->name && b->name && strcmp (a->name, b->name);
}

static int compareNameLen(const RAnalFunction *a, const RAnalFunction *b) {
	return a && b && a->name && b->name && strlen (a->name) > strlen (b->name);
}

static int compareAddress(const RAnalFunction *a, const RAnalFunction *b) {
	return a && b && a->addr && b->addr && a->addr > b->addr;
}

static int compareType(const RAnalFunction *a, const RAnalFunction *b) {
	return a && b && a->diff->type && b->diff->type && a->diff->type > b->diff->type;
}

static int compareSize(const RAnalFunction *a, const RAnalFunction *b) {
	// return a && b && a->_size < b->_size;
	return a && b && r_anal_fcn_realsize (a) > r_anal_fcn_realsize (b);
}

static int compareDist(const RAnalFunction *a, const RAnalFunction *b) {
	return a && b && a->diff->dist && b->diff->dist && a->diff->dist > b->diff->dist;
}

static int cb_diff_sort(void *_core, void *_node) {
	RConfigNode *node = _node;
	const char *column = node->value;
	RCore *core = _core;
	if (column && strcmp (column, "?")) {
		if (!strcmp (column, "name")) {
			core->anal->columnSort = (RListComparator)compareName;
		} else if (!strcmp (column, "namelen")) {
			core->anal->columnSort = (RListComparator)compareNameLen;
		} else if (!strcmp (column, "addr")) {
			core->anal->columnSort = (RListComparator)compareAddress;
		} else if (!strcmp (column, "type")) {
			core->anal->columnSort = (RListComparator)compareType;
		} else if (!strcmp (column, "size")) {
			core->anal->columnSort = (RListComparator)compareSize;
		} else if (!strcmp (column, "dist")) {
			core->anal->columnSort = (RListComparator)compareDist;
		} else {
			goto fail;
		}
		return true;
	}
fail:
	eprintf ("e diff.sort = [name, namelen, addr, type, size, dist]\n");
	return false;
}

static const char *has_esil(RCore *core, const char *name) {
	RListIter *iter;
	RAnalPlugin *h;
	if (!core || !core->anal || !name) {
		return NULL;
	}
	RAnal *a = core->anal;
	r_list_foreach (a->plugins, iter, h) {
		if (!strcmp (name, h->name)) {
			return h->esil? "Ae": "A_";
		}
	}
	return "__";
}

// copypasta from binr/rasm2/rasm2.c
static void rasm2_list(RCore *core, const char *arch, int fmt) {
	int i;
	const char *feat2, *feat;
	RAsm *a = core->assembler;
	char bits[32];
	RAsmPlugin *h;
	RListIter *iter;
	if (fmt == 'j') {
		r_cons_print ("{");
	}
	r_list_foreach (a->plugins, iter, h) {
		if (arch && *arch) {
			if (h->cpus && !strcmp (arch, h->name)) {
				char *c = strdup (h->cpus);
				int n = r_str_split (c, ',');
				for (i = 0; i < n; i++) {
					r_cons_println (r_str_word_get0 (c, i));
				}
				free (c);
				break;
			}
		} else {
			bits[0] = 0;
			/* The underscore makes it easier to distinguish the
			 * columns */
			if (h->bits&8) strcat (bits, "_8");
			if (h->bits&16) strcat (bits, "_16");
			if (h->bits&32) strcat (bits, "_32");
			if (h->bits&64) strcat (bits, "_64");
			if (!*bits) strcat (bits, "_0");
			feat = "__";
			if (h->assemble && h->disassemble)  feat = "ad";
			if (h->assemble && !h->disassemble) feat = "a_";
			if (!h->assemble && h->disassemble) feat = "_d";
			feat2 = has_esil (core, h->name);
			if (fmt == 'q') {
				r_cons_println (h->name);
			} else if (fmt == 'j') {
				const char *str_bits = "32, 64";
				const char *license = "GPL";
				r_cons_printf ("\"%s\":{\"bits\":[%s],\"license\":\"%s\",\"description\":\"%s\",\"features\":\"%s\"}%s",
						h->name, str_bits, license, h->desc, feat, iter->n? ",": "");
			} else {
				r_cons_printf ("%s%s  %-9s  %-11s %-7s %s\n",
						feat, feat2, bits, h->name,
						h->license?h->license:"unknown", h->desc);
			}
		}
	}
	if (fmt == 'j') {
		r_cons_print ("}\n");
	}
}

static inline void __setsegoff(RConfig *cfg, const char *asmarch, int asmbits) {
	int autoseg = (!strncmp (asmarch, "x86", 3) && asmbits == 16);
	r_config_set (cfg, "asm.segoff", r_str_bool (autoseg));
}

static int cb_debug_hitinfo(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->hitinfo = node->i_value;
	return true;
}

static int cb_analeobjmp(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.eobjmp = node->i_value;
	return true;
}

static int cb_analafterjmp(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.afterjmp = node->i_value;
	return true;
}

static int cb_analstrings(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (node->i_value) {
		r_config_set (core->config, "bin.strings", "false");
	}
	return true;
}

static int cb_analsleep(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->sleep = node->i_value;
	return true;
}

static int cb_analmaxrefs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->maxreflines = node->i_value;
	return true;
}

static int cb_analnopskip (void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.nopskip = node->i_value;
	return true;
}

static int cb_analhpskip (void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.hpskip = node->i_value;
	return true;
}

static int cb_analbbsplit (void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.bbsplit = node->i_value;
	return true;
}

/* obey section permissions */
static int cb_analnoncode(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.noncode = !!node->i_value;
	return true;
}

static void update_analarch_options(RCore *core, RConfigNode *node) {
	RAnalPlugin *h;
	RListIter *it;
	r_list_purge (node->options);
	r_list_foreach (core->anal->plugins, it, h) {
		SETOPTIONS (node, h->name, NULL);
	}
}

static int cb_analarch(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value == '?') {
		update_analarch_options (core, node);
		print_node_options (node);
		return false;
	}
	if (*node->value) {
		if (r_anal_use (core->anal, node->value)) {
			return true;
		}
		const char *aa = r_config_get (core->config, "asm.arch");
		if (!aa || strcmp (aa, node->value)) {
			eprintf ("anal.arch: cannot find '%s'\n", node->value);
		}
	}
	return false;
}

static int cb_analcpu(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	r_anal_set_cpu (core->anal, node->value);
	return true;
}

static int cb_analsplit(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->split = node->i_value;
	return true;
}

static int cb_analrecont(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.recont = node->i_value;
	return true;
}

static int cb_asmminvalsub(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->parser->minval = node->i_value;
	return true;
}

static int cb_asmsecsub(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_SECSUB;
	} else {
		core->print->flags &= (~R_PRINT_FLAGS_SECSUB);
	}
	r_print_set_flags (core->print, core->print->flags);
	return true;
}

static int cb_asmassembler(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	r_asm_use_assembler (core->assembler, node->value);
	return true;
}

static void update_asmcpu_options(RCore *core, RConfigNode *node) {
	RAsmPlugin *h;
	RListIter *iter;
	if (!core || !core->assembler) {
		return;
	}
	const char *arch = r_config_get (core->config, "asm.arch");
	if (!arch || !*arch) {
		return;
	}
	r_list_purge (node->options);
	r_list_foreach (core->assembler->plugins, iter, h) {
		if (h->cpus && !strcmp (arch, h->name)) {
			char *c = strdup (h->cpus);
			int i, n = r_str_split (c, ',');
			for (i = 0; i < n; i++) {
				SETOPTIONS (node, r_str_word_get0 (c, i), NULL);
			}
			free (c);
		}
	}
}

static int cb_asmcpu(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (*node->value == '?') {
		update_asmcpu_options (core, node);
		/* print verbose help instead of plain option listing */
		rasm2_list (core, r_config_get (core->config, "asm.arch"), node->value[1]);
		return 0;
	}
	r_asm_set_cpu (core->assembler, node->value);
	r_config_set (core->config, "anal.cpu", node->value);
	return true;
}

static void update_asmarch_options(RCore *core, RConfigNode *node) {
	RAsmPlugin *h;
	RListIter *iter;
	r_list_purge (node->options);
	r_list_foreach (core->assembler->plugins, iter, h) {
		SETOPTIONS (node, h->name, NULL);
	}
}

static int cb_asmarch(void *user, void *data) {
	char asmparser[32];
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	const char *asmos = NULL;
	int bits = R_SYS_BITS;
	if (!*node->value || !core || !core->assembler) {
		return false;
	}
	asmos = r_config_get (core->config, "asm.os");
	if (core && core->anal && core->anal->bits) {
		bits = core->anal->bits;
	}
	if (*node->value == '?') {
		update_asmarch_options (core, node);
		/* print more verbose help instead of plain option values */
		rasm2_list (core, NULL, node->value[1]);
		return false;
	}
	r_egg_setup (core->egg, node->value, bits, 0, R_SYS_OS);

	if (!r_asm_use (core->assembler, node->value)) {
		eprintf ("asm.arch: cannot find (%s)\n", node->value);
		return false;
	}
	//we should strdup here otherwise will crash if any r_config_set
	//free the old value
	char *asm_cpu = strdup (r_config_get (core->config, "asm.cpu"));
	if (core->assembler->cur) {
		const char *newAsmCPU = core->assembler->cur->cpus;
		if (newAsmCPU) {
			if (*newAsmCPU) {
				char *nac = strdup (newAsmCPU);
				char *comma = strchr (nac, ',');
				if (comma) {
					*comma = 0;
					r_config_set (core->config, "asm.cpu", nac);
				}
				free (nac);
			} else {
				r_config_set (core->config, "asm.cpu", "");
			}
		}
		bits = core->assembler->cur->bits;
		if (8 & bits) {
			bits = 8;
		} else if (16 & bits) {
			bits = 16;
		} else if (32 & bits) {
			bits = 32;
		} else {
			bits = 64;
		}
	}
	snprintf (asmparser, sizeof (asmparser), "%s.pseudo", node->value);
	r_config_set (core->config, "asm.parser", asmparser);
	if (core->assembler->cur && core->anal &&
	    !(core->assembler->cur->bits & core->anal->bits)) {
		r_config_set_i (core->config, "asm.bits", bits);
	}

	//r_debug_set_arch (core->dbg, r_sys_arch_id (node->value), bits);
	r_debug_set_arch (core->dbg, node->value, bits);
	if (!r_config_set (core->config, "anal.arch", node->value)) {
		char *p, *s = strdup (node->value);
		if (s) {
			p = strchr (s, '.');
			if (p) {
				*p = 0;
			}
			if (!r_config_set (core->config, "anal.arch", s)) {
				/* fall back to the anal.null plugin */
				r_config_set (core->config, "anal.arch", "null");
			}
			free (s);
		}
	}
	// set pcalign
	{
		int v = r_anal_archinfo (core->anal, R_ANAL_ARCHINFO_ALIGN);
		if (v != -1) {
			r_config_set_i (core->config, "asm.pcalign", v);
		} else {
			r_config_set_i (core->config, "asm.pcalign", 0);
		}
	}
	if (core->anal) {
		if (!r_syscall_setup (core->anal->syscall, node->value, asmos, core->anal->bits)) {
			//eprintf ("asm.arch: Cannot setup syscall '%s/%s' from '%s'\n",
			//	node->value, asmos, R2_LIBDIR"/radare2/"R2_VERSION"/syscall");
		}
	}
	//if (!strcmp (node->value, "bf"))
	//	r_config_set (core->config, "dbg.backend", "bf");
	__setsegoff (core->config, node->value, core->assembler->bits);

	// set a default endianness
	int bigbin = r_bin_is_big_endian (core->bin);
	if (bigbin == -1 /* error: no endianness detected in binary */) {
		// try to set RAsm to LE
		r_asm_set_big_endian (core->assembler, false);
		// set endian of display to LE
		core->print->big_endian = false;
	} else {
		// try to set endian of RAsm to match binary
		r_asm_set_big_endian (core->assembler, bigbin);
		// set endian of display to match binary
		core->print->big_endian = bigbin;
	}
	r_asm_set_cpu (core->assembler, asm_cpu);
	free (asm_cpu);
	RConfigNode *asmcpu = r_config_node_get (core->config, "asm.cpu");
	if (asmcpu) {
		update_asmcpu_options (core, asmcpu);
	}
	/* reload types and cc info */
	// changing asm.arch changes anal.arch
	// changing anal.arch sets types db
	// so ressetting is redundant and may lead to bugs
	// 1 case this is usefull is when sdb_types is null
	if (!core->anal || !core->anal->sdb_types) {
		r_core_anal_type_init (core);
	}
	r_core_anal_cc_init (core);
	return true;
}

static int cb_dbgbpsize(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->bpsize = node->i_value;
	return true;
}

static int cb_dbgbtdepth(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->btdepth = node->i_value;
	return true;
}

static int cb_asmbits(void *user, void *data) {
	const char *asmos, *asmarch;
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	int ret = 0, bits;
	if (!core) {
		eprintf ("user can't be NULL\n");
		return false;
	}

	bits = node->i_value;

	if (bits > 0) {
		ret = r_asm_set_bits (core->assembler, bits);
		if (!ret) {
			RAsmPlugin *h = core->assembler->cur;
			if (h) {
				eprintf ("Cannot set bits %d to '%s'\n", bits, h->name);
			} else {
				eprintf ("e asm.bits: Cannot set value, no plugins defined yet\n");
				ret = true;
			}
		}
		if (!r_anal_set_bits (core->anal, bits)) {
			eprintf ("asm.arch: Cannot setup '%d' bits analysis engine\n", bits);
		}
		core->print->bits = bits;
	}
	if (core->dbg && core->anal && core->anal->cur) {
		bool load_from_debug = false;
		r_debug_set_arch (core->dbg, core->anal->cur->arch, bits);
		if (r_config_get_i (core->config, "cfg.debug")) {
			load_from_debug = true;
		} else {
			(void)r_anal_set_reg_profile (core->anal);
		}
		if (load_from_debug) {
			if (core->dbg->h && core->dbg->h->reg_profile) {
#if __WINDOWS__
#if !defined(__MINGW64__) && !defined(_WIN64)
				core->dbg->bits = R_SYS_BITS_32;
#else
				core->dbg->bits = R_SYS_BITS_64;
#endif
#endif
				char *rp = core->dbg->h->reg_profile (core->dbg);
				r_reg_set_profile_string (core->dbg->reg, rp);
				r_reg_set_profile_string (core->anal->reg, rp);
				free (rp);
			}
		}
	}

	asmos = r_config_get (core->config, "asm.os");
	asmarch = r_config_get (core->config, "asm.arch");
	if (core->anal) {
		if (!r_syscall_setup (core->anal->syscall, asmarch, asmos, bits)) {
			//eprintf ("asm.arch: Cannot setup syscall '%s/%s' from '%s'\n",
			//	node->value, asmos, R2_LIBDIR"/radare2/"R2_VERSION"/syscall");
		}
		__setsegoff (core->config, asmarch, core->anal->bits);
		if (core->dbg) {
			r_bp_use (core->dbg->bp, asmarch, core->anal->bits);
		}
	}
	/* set pcalign */
	{
		int v = r_anal_archinfo (core->anal, R_ANAL_ARCHINFO_ALIGN);
		if (v != -1) {
			r_config_set_i (core->config, "asm.pcalign", v);
		} else {
			r_config_set_i (core->config, "asm.pcalign", 0);
		}
	}
	return ret;
}

static void update_asmfeatures_options(RCore *core, RConfigNode *node) {
	int i, argc;
	char *features;

	if (core && core->assembler && core->assembler->cur) {
		if (core->assembler->cur->features) {
			features = strdup (core->assembler->cur->features);
			argc = r_str_split (features, ',');
			for (i = 0; i < argc; i++) {
				const char *feature = r_str_word_get0 (features, i);
				SETOPTIONS (node, feature, NULL);
			}
			free (features);
		}
	}
}

static int cb_asmfeatures(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (*node->value == '?') {
		update_asmfeatures_options (core, node);
		print_node_options (node);
		return 0;
	}
	free (core->assembler->features);
	core->assembler->features = NULL;
	if (node->value[0]) {
		core->assembler->features = strdup (node->value);
	}
	return 1;
}

static int cb_asmlineswidth(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->anal->lineswidth = node->i_value;
	return true;
}

static int cb_emustr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		r_config_set (core->config, "asm.emu", "true");
	}
	return true;
}

static int cb_emuskip(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value == '?') {
		if (strlen (node->value) > 1 && node->value[1] == '?') {
			r_cons_printf ("Concatenation of meta types encoded as characters:\n" \
				"'d': data\n'c': code\n's': string\n'f': format\n'm': magic\n" \
				"'h': hide\n'C': comment\n'r': run\n" \
				"(default is 'ds' to skip data and strings)\n");
		} else {
			print_node_options (node);
		}
		return false;
	}
	return true;
}

static int cb_asm_invhex(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->assembler->invhex = node->i_value;
	return true;
}

static int cb_asm_pcalign(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	int align = node->i_value;
	if (align < 0) {
		align = 0;
	}
	core->assembler->pcalign = align;
	core->anal->pcalign = align;
	return true;
}

static int cb_asmos(void *user, void *data) {
	RCore *core = (RCore*) user;
	int asmbits = r_config_get_i (core->config, "asm.bits");
	RConfigNode *asmarch, *node = (RConfigNode*) data;

	if (*node->value == '?') {
		print_node_options (node);
		return 0;
	}
	if (!node->value[0]) {
		free (node->value);
		node->value = strdup (R_SYS_OS);
	}
	asmarch = r_config_node_get (core->config, "asm.arch");
	if (asmarch) {
		r_syscall_setup (core->anal->syscall, asmarch->value,
				node->value, core->anal->bits);
		__setsegoff (core->config, asmarch->value, asmbits);
	}
	r_anal_set_os (core->anal, node->value);
	return true;
}

static int cb_asmparser(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	return r_parse_use (core->parser, node->value);
}

static int cb_asmstrenc (void *user, void *data) {
	RConfigNode *node = (RConfigNode *)data;
	if (node->value[0] == '?') {
		print_node_options (node);
		r_cons_printf ("  -- if string's 2nd byte is 0 then utf16le else latin1\n");
		return false;
	}
	return true;
}

static int cb_binfilter(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->bin->filter = node->i_value;
	return true;
}

/* BinDemangleCmd */
static int cb_bdc(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->bin->demanglercmd = node->i_value;
	return true;
}


static int cb_strpurge(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->bin->strpurge = node->i_value;
	return true;
}

static int cb_midflags (void *user, void *data) {
	RConfigNode *node = (RConfigNode *)data;
	if (node->value[0] == '?') {
		print_node_options (node);
		return false;
	}
	return true;
}

static int cb_strfilter(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (node->value[0] == '?') {
		if (strlen (node->value) > 1 && node->value[1] == '?') {
			r_cons_printf ("Valid values for bin.strfilter:\n"
				"a  only alphanumeric printable\n"
				"8  only strings with utf8 chars\n"
				"p  file/directory paths\n"
				"e  email-like addresses\n"
				"u  urls\n"
				"i  IPv4 address-like strings\n"
				"U  only uppercase strings\n"
				"f  format-strings\n");
		} else {
			print_node_options (node);
		}
		return false;
	} else {
		core->bin->strfilter = node->value[0];
	}
	return true;
}

static int cb_binforce(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	r_bin_force_plugin (core->bin, node->value);
	return true;
}

static int cb_asmsyntax(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value == '?') {
		print_node_options (node);
		return false;
	} else {
		int syntax = r_asm_syntax_from_string (node->value);
		if (syntax == -1) {
			return false;
		}
		r_asm_set_syntax (core->assembler, syntax);
	}
	return true;
}

static int cb_dirzigns(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	free (core->anal->zign_path);
	core->anal->zign_path = strdup (node->value);
	return true;
}

static int cb_bigendian(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	// Try to set endian based on preference, restrict by RAsmPlugin
	bool isbig = r_asm_set_big_endian (core->assembler, node->i_value);
	// Set anal endianness the same as asm
	r_anal_set_big_endian (core->anal, isbig);
	// the big endian should also be assigned to dbg->bp->endian
	core->dbg->bp->endian = isbig;
	// Set printing endian to user's choice
	core->print->big_endian = node->i_value;
	return true;
}

static int cb_cfgdatefmt(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	snprintf (core->print->datefmt, 32, "%s", node->value);
	return true;
}

static int cb_timezone(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->print->datezone = node->i_value;
	return true;
}

static int cb_cfglog(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->cfglog = node->i_value;
	return true;
}

static int cb_cfgdebug(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (!core) return false;
	if (core->io) {
		core->io->debug = node->i_value;
	}
	if (core->dbg && node->i_value) {
		const char *dbgbackend = r_config_get (core->config, "dbg.backend");
		core->bin->is_debugger = true;
		r_debug_use (core->dbg, dbgbackend);
		if (!strcmp (r_config_get (core->config, "cmd.prompt"), "")) {
			r_config_set (core->config, "cmd.prompt", ".dr*");
		}
		if (!strcmp (dbgbackend, "bf"))
			r_config_set (core->config, "asm.arch", "bf");
		if (core->file) {
#if __WINDOWS__
			r_debug_select (core->dbg, core->dbg->pid,
					core->dbg->tid);
#else
			r_debug_select (core->dbg, core->file->desc->fd,
					core->file->desc->fd);
#endif
		}
	} else {
		if (core->dbg) r_debug_use (core->dbg, NULL);
		core->bin->is_debugger = false;
	}
	return true;
}

static int cb_dirsrc(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	RCore *core = (RCore *)user;
	free (core->bin->srcdir);
	core->bin->srcdir = strdup (node->value);
	return true;
}

static int cb_cfgsanbox(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	int ret = r_sandbox_enable (node->i_value);
	if (node->i_value != ret) {
		eprintf ("Cannot disable sandbox\n");
	}
	return (!node->i_value && ret)? 0: 1;
}

static int cb_cmdlog(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	R_FREE (core->cmdlog);
	core->cmdlog = strdup (node->value);
	return true;
}

static int cb_cmdrepeat(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->cmdrepeat = node->i_value;
	return true;
}

static int cb_scrnull(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->cons->null = node->i_value;
	return true;
}

static int cb_color(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_COLOR;
	} else {
		//c:core->print->flags ^= R_PRINT_FLAGS_COLOR;
		core->print->flags &= (~R_PRINT_FLAGS_COLOR);
	}
	r_cons_singleton ()->use_color = node->i_value? 1: 0;
	r_print_set_flags (core->print, core->print->flags);
	return true;
}

static int cb_decoff(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_ADDRDEC;
	} else {
		core->print->flags &= (~R_PRINT_FLAGS_ADDRDEC);
	}
	r_print_set_flags (core->print, core->print->flags);
	return true;
}

static int cb_dbgbep(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value == '?') {
		print_node_options (node);
		return false;
	}
	return true;
}

static int cb_dbg_btalgo(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value == '?') {
		print_node_options (node);
		return false;
	}
	free (core->dbg->btalgo);
	core->dbg->btalgo = strdup (node->value);
	return true;
}

static int cb_dbg_libs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	free (core->dbg->glob_libs);
	core->dbg->glob_libs = strdup (node->value);
	return true;
}

static int cb_dbg_unlibs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	free (core->dbg->glob_unlibs);
	core->dbg->glob_unlibs = strdup (node->value);
	return true;
}

static int cb_dbg_forks(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->trace_forks = node->i_value;
	if (core->io->debug) {
		r_debug_attach (core->dbg, core->dbg->pid);
	}
	return true;
}

static int cb_dbg_execs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->trace_execs = node->i_value;
	if (core->io->debug) {
		r_debug_attach (core->dbg, core->dbg->pid);
	}
	return true;
}

static int cb_dbg_clone(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->trace_clone = node->i_value;
	if (core->io->debug) {
		r_debug_attach (core->dbg, core->dbg->pid);
	}
	return true;
}

static int cb_dbg_follow_child(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->follow_child = node->i_value;
	return true;
}

static int cb_dbg_aftersc(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->dbg->trace_aftersyscall = node->i_value;
	if (core->io->debug) {
		r_debug_attach (core->dbg, core->dbg->pid);
	}
	return true;
}

static int cb_runprofile(void *user, void *data) {
	RCore *r = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	free ((void*)r->io->runprofile);
	if (!node || !*(node->value)) {
		r->io->runprofile = NULL;
	} else {
		r->io->runprofile = strdup (node->value);
	}
	return true;
}

static int cb_dbg_args(void *user, void *data) {
	RCore *core = (RCore *)user;
	RConfigNode *node = (RConfigNode*) data;
	if (!node || !*(node->value)) {
		core->io->args = NULL;
	} else {
		core->io->args = strdup (node->value);
	}
	return true;
}

static int cb_dbgstatus(void *user, void *data) {
	RCore *r = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (r_config_get_i (r->config, "cfg.debug")) {
		if (node->i_value) {
			r_config_set (r->config, "cmd.prompt",
				".dr*; drd; sr PC;pi 1;s-");
		} else {
			r_config_set (r->config, "cmd.prompt", ".dr*");
		}
	}
	return true;
}

static int cb_dbgbackend(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	if (!strcmp (node->value, "?")) {
		r_debug_plugin_list (core->dbg, 'q');
		return false;
	}
	if (!strcmp (node->value, "bf")) {
		r_config_set (core->config, "asm.arch", "bf");
	}
	r_debug_use (core->dbg, node->value);
	return true;
}

static int cb_gotolimit(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode*) data;
	if (r_sandbox_enable (0)) {
		eprintf ("Cannot change gotolimit\n");
		return false;
	}
	if (core->anal->esil) {
		core->anal->esil_goto_limit = node->i_value;
	}
	return true;
}

static int cb_esilverbose (void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode*) data;
	if (core->anal->esil) {
		core->anal->esil->verbose = node->i_value;
	}
	return true;
}

static int cb_esilstackdepth (void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	if (node->i_value < 3) {
		eprintf ("esil.stack.depth must be greater than 2\n");
		node->i_value = 32;
	}
	return true;
}

static int cb_fixrows(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->fix_rows = (int)node->i_value;
	return true;
}

static int cb_fixcolumns(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->fix_columns = atoi (node->value);
	return true;
}

static int cb_rows(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->force_rows = node->i_value;
	return true;
}

static int cb_hexcompact(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_COMPACT;
	} else {
		core->print->flags &= (~R_PRINT_FLAGS_COMPACT);
	}
	return true;
}

static int cb_hexpairs(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->print->pairs = node->i_value;
	return true;
}

static int cb_hexcomments(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_COMMENT;
	} else {
		core->print->flags &= ~R_PRINT_FLAGS_COMMENT;
	}
	return true;
}

R_API bool r_core_esil_cmd(RAnalEsil *esil, const char *cmd, ut64 a1, ut64 a2) {
	if (cmd && *cmd) {
		RCore *core = esil->anal->user;
		r_core_cmdf (core, "%s %"PFMT64d" %" PFMT64d, cmd, a1, a2);
		return true;
	}
	return false;
}

static int cb_cmd_esil_ioer(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		free (core->anal->esil->cmd_ioer);
		core->anal->esil->cmd_ioer = strdup (node->value);
	}
	return true;
}

static int cb_cmd_esil_todo(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		free (core->anal->esil->cmd_todo);
		core->anal->esil->cmd_todo = strdup (node->value);
	}
	return true;
}

static int cb_cmd_esil_intr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		free (core->anal->esil->cmd_intr);
		core->anal->esil->cmd_intr = strdup (node->value);
	}
	return true;
}

static int cb_mdevrange(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		free (core->anal->esil->mdev_range);
		core->anal->esil->mdev_range = strdup (node->value);
	}
	return true;
}

static int cb_cmd_esil_mdev(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		free (core->anal->esil->cmd_mdev);
		core->anal->esil->cmd_mdev = strdup (node->value);
	}
	return true;
}

static int cb_cmd_esil_trap(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core && core->anal && core->anal->esil) {
		core->anal->esil->cmd = r_core_esil_cmd;
		core->anal->esil->cmd_trap = strdup (node->value);
	}
	return true;
}

static int cb_fsview(void *user, void *data) {
	int type = R_FS_VIEW_NORMAL;
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (*node->value == '?') {
		print_node_options (node);
		return false;
	}
	if (!strcmp (node->value, "all")) {
		type = R_FS_VIEW_ALL;
	}
	if (!strstr (node->value, "del")) {
		type |= R_FS_VIEW_DELETED;
	}
	if (!strstr (node->value, "spe")) {
		type |= R_FS_VIEW_SPECIAL;
	}
	r_fs_view (core->fs, type);
	return true;
}

static int cb_cmddepth(void *user, void *data) {
	int c = R_MAX (((RConfigNode*)data)->i_value, 0);
	((RCore *)user)->cmd_depth = c;
	return true;
}

static int cb_hexcols(void *user, void *data) {
	RCore *core = (RCore *)user;
	int c = R_MIN (1024, R_MAX (((RConfigNode*)data)->i_value, 0));
	if (c < 0) {
		c = 0;
	}
	core->print->cols = c & ~1;
	core->dbg->regcols = c/4;
	return true;
}

static int cb_hexstride(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	((RCore *)user)->print->stride = node->i_value;
	return true;
}

static int cb_search_kwidx(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->search->n_kws = node->i_value;
	return true;
}

static int cb_ioenforce(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	int perm = node->i_value;
	core->io->enforce_rwx = 0;
	if (perm & 1) {
		core->io->enforce_rwx |= R_IO_READ;
	}
	if (perm & 2) {
		core->io->enforce_rwx |= R_IO_WRITE;
	}
	return true;
}

static int cb_iosectonly(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->io->sectonly = node->i_value? 1: 0;
	return true;
}

static int cb_iobuffer(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		ut64 from, to;
		from = r_config_get_i (core->config, "io.buffer.from");
		to = r_config_get_i (core->config, "io.buffer.to");
		if (from>=to) {
			eprintf ("ERROR: io.buffer.from >= io.buffer.to"
					" (0x%"PFMT64x" >= 0x%"PFMT64x")\n", from, to);
		} else r_io_buffer_load (core->io, from, (int)(to-from));
	} else {
		r_io_buffer_close (core->io);
	}
	r_core_block_read (core);
	return true;
}

static int cb_iocache(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if ((int)node->i_value < 0) {
		r_io_cache_reset (core->io, node->i_value);
	}
	r_io_cache_enable (core->io, node->i_value, node->i_value);
	return true;
}

static int cb_ioaslr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value != core->io->aslr) {
		core->io->aslr = node->i_value;
	}
	return true;
}

static int cb_iova(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value != core->io->va) {
		core->io->va = node->i_value;
		/* ugly fix for r2 -d ... "r2 is going to die soon ..." */
		if (r_io_desc_get (core->io, core->io->raised)) {
			r_core_block_read (core);
		}
#if 0
		/* reload symbol information */
		if (r_list_length (r_bin_get_sections (core->bin)) > 0) {
			r_core_cmd0 (core, ".ia*");
		}
#endif
	}
	return true;
}

static int cb_iopava(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->io->pava = node->i_value;
	return true;
}

static int cb_ioff(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->io->ff = node->i_value;
	return true;
}

static int cb_io_oxff(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->io->Oxff = node->i_value;
	return true;
}

static int cb_filepath(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	r_config_set (core->config, "file.lastpath", node->value);
	char *pikaboo = strstr (node->value, "://");
	if (pikaboo) {
		if (pikaboo[3] == '/') {
			char *ovalue = node->value;
			node->value = strdup (pikaboo + 3);
			free (ovalue);
			return true;
		}
		return false;
	}
	return true;
}

static int cb_ioautofd(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->io->autofd = node->i_value;
	return true;
}

static int cb_pager(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;

	/* Let cons know we have a new pager. */
	core->cons->pager = node->value;
	return true;
}

static int cb_breaklines(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->break_lines = node->i_value;
	return true;
}

static int cb_fps(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->fps = node->i_value;
	return true;
}

static int cb_rgbcolors(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	RCore *core = (RCore *) user;
	if (node->i_value) {
		r_cons_singleton()->truecolor =
			(r_config_get_i (core->config, "scr.truecolor"))?2:1;
	} else {
		r_cons_singleton()->truecolor = 0;
	}
	return true;
}

static int cb_scrbreakword(void* user, void* data) {
	RConfigNode *node = (RConfigNode*) data;
	if (*node->value) {
		r_cons_breakword (node->value);
	} else {
		r_cons_breakword (NULL);
	}
	return true;
}

static int cb_scrcolumns(void* user, void* data) {
	RConfigNode *node = (RConfigNode*) data;
	RCore *core = (RCore*) user;
	int n = atoi (node->value);
	core->cons->force_columns = n;
	core->dbg->regcols = n / 20;
	return true;
}

static int cb_scrfgets(void* user, void* data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode*) data;
	core->cons->user_fgets = node->i_value
		? NULL : (void *)r_core_fgets;
	return true;
}

static int cb_scrhtml(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton ()->is_html = node->i_value;
	// TODO: control error and restore old value (return false?) show errormsg?
	return true;
}

static int cb_scrhighlight(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_highlight (node->value);
	return true;
}

#if __WINDOWS__ && !__CYGWIN__
static int scr_ansicon(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->ansicon = node->i_value;
	return true;
}
#endif

static int cb_screcho(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->echo = node->i_value;
	return true;
}

static int cb_scrlinesleep(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->linesleep = node->i_value;
	return true;
}

static int cb_scrpagesize(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->pagesize= node->i_value;
	return true;
}

static int cb_scrflush(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->flush = node->i_value;
	return true;
}

static int cb_exectrap(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	RCore *core = (RCore*) user;
	if (core->anal && core->anal->esil) {
		core->anal->esil->exectrap = node->i_value;
	}
	return true;
}

static int cb_iotrap(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	RCore *core = (RCore*) user;
	if (core->anal && core->anal->esil) {
		core->anal->esil->iotrap = node->i_value;
	}
	return true;
}

static int cb_scrint(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value && r_sandbox_enable (0)) {
		return false;
	}
	r_cons_singleton()->is_interactive = node->i_value;
	return true;
}

static int cb_scrnkey(void *user, void *data) {
	RConfigNode *node = (RConfigNode*) data;
	if (!strcmp (node->value, "help") || *node->value == '?') {
		print_node_options (node);
		return false;
	}
	return true;
}

static int cb_scrprompt(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_line_singleton()->echo = node->i_value;
	return true;
}

static int cb_scrrows(void* user, void* data) {
	RConfigNode *node = (RConfigNode*) data;
	int n = atoi (node->value);
	((RCore *)user)->cons->force_rows = n;
	return true;
}

static int cb_contiguous(void *user, void *data) {
	RCore *core = (RCore *)user;
	RConfigNode *node = (RConfigNode *) data;
	core->search->contiguous = node->i_value;
	return true;
}

static int cb_searchalign(void *user, void *data) {
	RCore *core = (RCore *)user;
	RConfigNode *node = (RConfigNode *) data;
	core->search->align = node->i_value;
	core->print->addrmod = node->i_value;
	return true;
}

static int cb_segoff(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->print->flags |= R_PRINT_FLAGS_SEGOFF;
	} else {
		core->print->flags &= (((ut32)-1) & (~R_PRINT_FLAGS_SEGOFF));
	}
	return true;
}

static int cb_stopthreads(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->stop_all_threads = node->i_value;
	return true;
}

static int cb_swstep(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->swstep = node->i_value;
	return true;
}

static int cb_consbreak(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->consbreak = node->i_value;
	return true;
}

static int cb_teefile(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	r_cons_singleton()->teefile = node->value;
	return true;
}

static int cb_anal_trace(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core->anal) {
		if (node->i_value && !core->anal->esil) {
			r_core_cmd0 (core, "aei");
		}
		core->anal->trace = node->i_value;
	}
	return true;
}

static int cb_trace(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->trace->enabled = node->i_value;
	return true;
}

static int cb_tracetag(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->dbg->trace->tag = node->i_value;
	return true;
}

static int cb_truecolor(void *user, void *data) {
	RConfigNode *node = (RConfigNode *) data;
	if (r_cons_singleton()->truecolor)
		r_cons_singleton()->truecolor = (node->i_value)? 2: 1;
	return true;
}

static int cb_utf8(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->cons->use_utf8 = node->i_value;
	return true;
}

static int cb_zoombyte(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	switch (*node->value) {
		case 'p': case 'f': case 's': case '0':
		case 'F': case 'e': case 'h':
			core->print->zoom->mode = *node->value;
			break;
		default:
			r_cons_printf ("p\nf\ns\n0\nF\ne\nh\n");
			// eprintf ("Invalid zoom.byte value. See pz? for help\n");
			return false;
	}
	return true;
}

static int cb_binverbose(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->bin->verbose = node->i_value;
	return true;
}

static int cb_rawstr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->bin->rawstr = node->i_value;
	return true;
}

static int cb_debase64(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	core->bin->debase64 = node->i_value;
	return true;
}

static int cb_binstrings(void *user, void *data) {
	const ut32 req = R_BIN_REQ_STRINGS;
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (node->i_value) {
		core->bin->filter_rules |= req;
	} else {
		core->bin->filter_rules &= ~req;
	}
	return true;
}

static int cb_bindbginfo(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (!core || !core->bin) {
		return false;
	}
	core->bin->want_dbginfo = node->i_value;
	return true;
}

static int cb_binprefix(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (!core || !core->bin) {
		return false;
	}
	if (node->value && *node->value) {
		if (!strcmp (node->value, "auto")) {
			if (!core->bin->file) {
				return false;
			}
			char *name = (char *)r_file_basename (core->bin->file);
			if (name) {
				r_name_filter (name, strlen (name));
				r_str_filter (name, strlen (name));
				core->bin->prefix = strdup (name);
				free (name);
			}
		} else {
			core->bin->prefix = node->value;
		}
	}
	return true;
}

static int cb_binmaxstrbuf(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core->bin) {
		int v = node->i_value;
		ut64 old_v = core->bin->maxstrbuf;
		if (v < 1) {
			v = 4; // HACK
		}
		core->bin->maxstrbuf = v;
		if (v>old_v) {
			r_core_bin_refresh_strings (core);
		}
		return true;
	}
	return true;
}

static int cb_binmaxstr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core->bin) {
		int v = node->i_value;
		if (v<1) v = 4; // HACK
		core->bin->maxstrlen = v;
	// TODO: Do not refresh if nothing changed (minstrlen ?)
		r_core_bin_refresh_strings (core);
		return true;
	}
	return true;
}

static int cb_binminstr(void *user, void *data) {
	RCore *core = (RCore *) user;
	RConfigNode *node = (RConfigNode *) data;
	if (core->bin) {
		int v = node->i_value;
		if (v<1) v = 4; // HACK
		core->bin->minstrlen = v;
	// TODO: Do not refresh if nothing changed (minstrlen ?)
		r_core_bin_refresh_strings (core);
		return true;
	}
	return true;
}

static int cb_searchin(void *user, void *data) {
 	RConfigNode *node = (RConfigNode*) data;
 	if (*node->value == '?') {
		print_node_options (node);
 		return false;
 	}
 	return true;
}

static int cb_fileloadmethod(void *user, void *data) {
 	RConfigNode *node = (RConfigNode*) data;
 	if (*node->value == '?') {
		print_node_options (node);
 		return false;
 	}
 	return true;
}

static int __dbg_swstep_getter(void *user, RConfigNode *node) {
	RCore *core = (RCore*)user;
	node->i_value = core->dbg->swstep;
	return true;
}

static int cb_anal_gp(RCore *core, RConfigNode *node) {
	core->anal->gp = node->i_value;
	return true;
}

static int cb_anal_from(RCore *core, RConfigNode *node) {
	if (r_config_get_i (core->config, "anal.limits")) {
		r_anal_set_limits (core->anal,
				r_config_get_i (core->config, "anal.from"),
				r_config_get_i (core->config, "anal.to"));
	}
	return true;
}

static int cb_anal_limits(void *user, RConfigNode *node) {
	RCore *core = (RCore*)user;
	if (node->i_value) {
		r_anal_set_limits (core->anal,
				r_config_get_i (core->config, "anal.from"),
				r_config_get_i (core->config, "anal.to"));
	} else {
		r_anal_unset_limits (core->anal);
	}
	return 1;
}

static int cb_anal_jmptbl(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.jmptbl = node->i_value;
	return true;
}

static int cb_anal_cjmpref(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.cjmpref = node->i_value;
	return true;
}

static int cb_anal_jmpref(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.jmpref = node->i_value;
	return true;
}

static int cb_anal_jmpabove(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.jmpabove = node->i_value;
	return true;
}

static int cb_anal_followdatarefs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.followdatarefs = node->i_value;
	return true;
}

static int cb_anal_searchstringrefs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.searchstringrefs = node->i_value;
	return true;
}

static int cb_anal_pushret(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.pushret = node->i_value;
	return true;
}

static int cb_anal_brokenrefs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.followbrokenfcnsrefs = node->i_value;
	return true;
}

static int cb_anal_bbs_alignment(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.bbs_alignment = node->i_value;
	return true;
}

static int cb_anal_bb_max_size(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->anal->opt.bb_max_size = node->i_value;
	return true;
}

static int cb_linesto(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	ut64 from = (ut64)r_config_get_i (core->config, "lines.from");
	int io_sz = r_io_size (core->io);
	ut64 to = r_num_math (core->num, node->value);
	if (to == 0) {
		core->print->lines_cache_sz = -1; //r_core_lines_initcache (core, from, to);
		return false;
	}
	if (to > from+io_sz) {
		eprintf ("ERROR: \"lines.to\" can't exceed addr 0x%08"PFMT64x
			" 0x%08"PFMT64x" %d\n", from, to, io_sz);
		return true;
	}
	if (to > from) {
		core->print->lines_cache_sz = r_core_lines_initcache (core, from, to);
		//if (core->print->lines_cache_sz == -1) { eprintf ("ERROR: Can't allocate memory\n"); }
	} else {
		eprintf ("Invalid range 0x%08"PFMT64x" .. 0x%08"PFMT64x"\n", from, to);
	}
	return true;
}

static int cb_linesabs(void *user, void *data) {
	RCore *core = (RCore*) user;
	RConfigNode *node = (RConfigNode*) data;
	core->print->lines_abs = node->i_value;
	if (core->print->lines_abs && core->print->lines_cache_sz <= 0) {
		ut64 from = (ut64)r_config_get_i (core->config, "lines.from");
		ut64 to = (ut64)r_config_get_i (core->config, "lines.to");
		core->print->lines_cache_sz = r_core_lines_initcache (core, from, to);
		if (core->print->lines_cache_sz == -1) {
			eprintf ("ERROR: \"lines.from\" and \"lines.to\" must be set\n");
		} else {
			eprintf ("Found %d lines\n", core->print->lines_cache_sz-1);
		}
	}
	return true;
}

static char *getViewerPath() {
	int i;
	const char *viewers[] = {
		"open",
		"geeqie",
		"gqview",
		"eog",
		"xdg-open",
		NULL
	};
	for (i = 0; viewers[i]; i++) {
		char *dotPath = r_file_path (viewers[i]);
		if (dotPath && strcmp (dotPath, viewers[i])) {
			return dotPath;
		}
		free (dotPath);
	}
	return NULL;
}

#define SLURP_LIMIT (10*1024*1024)
R_API int r_core_config_init(RCore *core) {
	int i;
	char buf[128], *p, *tmpdir;
	RConfigNode *n;
	RConfig *cfg = core->config = r_config_new (core);
	if (!cfg) {
		return 0;
	}
	cfg->cb_printf = r_cons_printf;
	cfg->num = core->num;
	/* pdb */
	SETPREF ("pdb.useragent", "Microsoft-Symbol-Server/6.11.0001.402", "User agent for Microsoft symbol server");
	SETPREF ("pdb.server", "https://msdl.microsoft.com/download/symbols", "Base URL for Microsoft symbol server");
	SETI ("pdb.extract", 1, "Avoid extract of the pdb file, just download");

	/* anal */
	SETPREF ("anal.fcnprefix", "fcn",  "Prefix new function names with this");
	SETPREF ("anal.a2f", "false",  "Use the new WIP analysis algorithm (core/p/a2f), anal.depth ignored atm");
	SETICB ("anal.gp", 0, (RConfigCallback)&cb_anal_gp, "Set the value of the GP register (MIPS)");
	SETCB ("anal.limits", "false", (RConfigCallback)&cb_anal_limits, "Restrict analysis to address range [anal.from:anal.to]");
	SETICB ("anal.from", -1, (RConfigCallback)&cb_anal_from, "Lower limit on the address range for analysis");
	SETICB ("anal.to", -1, (RConfigCallback)&cb_anal_from, "Upper limit on the address range for analysis");
	SETI ("anal.timeout", 0, "Stop analyzing after a couple of seconds");

	SETCB ("anal.eobjmp", "false", &cb_analeobjmp, "jmp is end of block mode (option)");
	SETCB ("anal.afterjmp", "true", &cb_analafterjmp, "Continue analysis after jmp/ujmp");
	SETI ("anal.depth", 16, "Max depth at code analysis"); // XXX: warn if depth is > 50 .. can be problematic
	SETICB ("anal.sleep", 0, &cb_analsleep, "Sleep N usecs every so often during analysis. Avoid 100% CPU usage");
	SETPREF ("anal.calls", "false", "Make basic af analysis walk into calls");
	SETPREF ("anal.autoname", "true", "Automatically set a name for the functions, may result in some false positives");
	SETPREF ("anal.hasnext", "false", "Continue analysis after each function");
	SETPREF ("anal.esil", "false", "Use the new ESIL code analysis");
	SETCB ("anal.strings", "false", &cb_analstrings, "Identify and register strings during analysis (aar only)");
	SETPREF ("anal.vars", "true",  "Analyze local variables and arguments");
	SETPREF ("anal.vinfun", "true",  "Search values in functions (aav) (false by default to only find on non-code)");
	SETPREF ("anal.vinfunrange", "false",  "Search values outside function ranges (requires anal.vinfun=false)\n");
	SETCB ("anal.nopskip", "true", &cb_analnopskip, "Skip nops at the beginning of functions");
	SETCB ("anal.hpskip", "false", &cb_analhpskip, "Skip `mov reg, reg` and `lea reg, [reg] at the beginning of functions");
	SETCB ("anal.noncode", "false", &cb_analnoncode, "Analyze data as code");
	n = NODECB ("anal.arch", R_SYS_ARCH, &cb_analarch);
	SETDESC (n, "Select the architecture to use");
	update_analarch_options (core, n);
	SETCB ("anal.cpu", R_SYS_ARCH, &cb_analcpu, "Specify the anal.cpu to use");
	SETPREF ("anal.prelude", "", "Specify an hexpair to find preludes in code");
	SETCB ("anal.split", "true", &cb_analsplit, "Split functions into basic blocks in analysis");
	SETCB ("anal.recont", "false", &cb_analrecont, "End block after splitting a basic block instead of error"); // testing
	SETCB ("anal.trace", "false", &cb_anal_trace, "Record ESIL trace in log database");
	SETI ("anal.ptrdepth", 3, "Maximum number of nested pointers to follow in analysis");
	SETICB ("anal.maxreflines", 0, &cb_analmaxrefs, "Maximum number of reflines to be analyzed and displayed in asm.lines with pd");

	SETCB ("anal.jmptbl", "false", &cb_anal_jmptbl, "Analyze jump tables in switch statements");

	SETCB ("anal.cjmpref", "false", &cb_anal_cjmpref, "Create references for conditional jumps");
	SETCB ("anal.jmpref", "true", &cb_anal_jmpref, "Create references for unconditional jumps");

	SETCB ("anal.jmpabove", "true", &cb_anal_jmpabove, "Jump above function pointer");
	SETCB ("anal.datarefs", "false", &cb_anal_followdatarefs, "Follow data references for code coverage");
	SETCB ("anal.brokenrefs", "false", &cb_anal_brokenrefs, "Follow function references as well if function analysis was failed");

	SETCB ("anal.searchstringrefs", "false", &cb_anal_searchstringrefs, "Search string references in data references");
	SETCB ("anal.bb.split", "true", &cb_analbbsplit, "Use the experimental basic block split for JMPs");
	SETCB ("anal.bb.align", "0x10", &cb_anal_bbs_alignment, "Possible space between basic blocks");
	SETCB ("anal.bb.maxsize", "1024", &cb_anal_bb_max_size, "Maximum basic block size");
	SETCB ("anal.pushret", "false", &cb_anal_pushret, "Analyze push+ret as jmp");

	SETPREF ("esil.prestep", "true", "Step before esil evaluation in `de` commands");
	SETPREF ("esil.fillstack", "", "Initialize ESIL stack with (random, debrujn, sequence, zeros, ...)");
	SETICB ("esil.verbose", 0, &cb_esilverbose, "Show ESIL verbose level (0, 1, 2)");
	SETICB ("esil.gotolimit", core->anal->esil_goto_limit, &cb_gotolimit, "Maximum number of gotos per ESIL expression");
	SETICB ("esil.stack.depth", 32, &cb_esilstackdepth, "Number of elements that can be pushed on the esilstack");
	SETI ("esil.stack.size", 0xf0000, "Number of elements that can be pushed on the esilstack");
	SETI ("esil.stack.addr", 0x100000, "Number of elements that can be pushed on the esilstack");
	SETPREF ("esil.stack.pattern", "0", "Specify fill pattern to initialize the stack (0, w, d, i)");

	/* asm */
	//asm.os needs to be first, since other asm.* depend on it
	n = NODECB ("asm.os", R_SYS_OS, &cb_asmos);
	SETDESC (n, "Select operating system (kernel)");
	SETOPTIONS (n, "ios", "dos", "darwin", "linux", "freebsd", "openbsd", "netbsd", "windows", NULL);
	SETI ("asm.maxrefs", 5,  "Maximum number of xrefs to be displayed as list (use columns above)");
	SETCB ("asm.invhex", "false", &cb_asm_invhex, "Show invalid instructions as hexadecimal numbers");
	SETPREF ("asm.bytes", "true", "Display the bytes of each instruction");
	SETPREF ("asm.flagsinbytes", "false",  "Display flags inside the bytes space");
	n = NODEICB ("asm.midflags", 2, &cb_midflags);
	SETDESC (n, "Realign disassembly if there is a flag in the middle of an instruction");
	SETPREF ("asm.midcursor", "false", "Cursor in visual disasm mode breaks the instruction");
	SETOPTIONS (n, "0 = do not show flag", "1 = show without realign", "2 = realign at middle flag",
		"3 = realign at middle flag if sym.*", NULL);
	SETPREF ("asm.cmtflgrefs", "true", "Show comment flags associated to branch reference");
	SETPREF ("asm.cmtright", "true", "Show comments at right of disassembly if they fit in screen");
	SETI ("asm.cmtcol", 71, "Column to align comments");
	SETICB ("asm.pcalign", 0, &cb_asm_pcalign, "Only recognize as valid instructions aligned to this value");
	SETPREF ("asm.calls", "true", "Show callee function related info as comments in disasm");
	SETPREF ("asm.bbline", "false", "Show empty line after every basic block");
	SETPREF ("asm.comments", "true", "Show comments in disassembly view");
	SETPREF ("asm.jmphints", "true", "Show jump hints [numbers] in disasm");
	SETPREF ("asm.leahints", "false", "Show LEA hints [numbers] in disasm");
	SETPREF ("asm.slow", "true", "Perform slow analysis operations in disasm");
	SETPREF ("asm.decode", "false", "Use code analysis as a disassembler");
	SETPREF ("asm.flgoff", "false", "Show offset in flags");
	SETPREF ("asm.indent", "false", "Indent disassembly based on reflines depth");
	SETI ("asm.indentspace", 2, "How many spaces to indent the code");
	SETPREF ("asm.dwarf", "false", "Show dwarf comment at disassembly");
	SETPREF ("asm.dwarf.abspath", "false", "Show absolute path in asm.dwarf");
	SETPREF ("asm.dwarf.file", "true", "Show filename of asm.dwarf in pd");
	SETPREF ("asm.esil", "false", "Show ESIL instead of mnemonic");
	SETPREF ("asm.nodup", "false", "Do not show dupped instructions (collapse disasm)");
	SETPREF ("asm.emu", "false", "Run ESIL emulation analysis on disasm");
	SETCB ("asm.emustr", "false", &cb_emustr, "Show only strings if any in the asm.emu output");
	SETPREF ("asm.emuwrite", "false", "Allow asm.emu to modify memory (WARNING)");
	n = NODECB ("asm.emuskip", "ds", &cb_emuskip);
	SETDESC (n, "Skip metadata of given types in asm.emu");
	SETOPTIONS (n, "d", "c", "s", "f", "m", "h", "C", "r", NULL);
	SETPREF ("asm.filter", "true", "Replace numeric values by flags (e.g. 0x4003e0 -> sym.imp.printf)");
	SETPREF ("asm.fcnlines", "true", "Show function boundary lines");
	SETPREF ("asm.flags", "true", "Show flags");
	SETPREF ("asm.lbytes", "true", "Align disasm bytes to left");
	SETPREF ("asm.lines", "true", "Show ASCII-art lines at disassembly");
	SETPREF ("asm.lines.call", "false", "Enable call lines");
	SETPREF ("asm.lines.ret", "false", "Show separator lines after ret");
	SETPREF ("asm.linesout", "true", "Show out of block lines");
	SETPREF ("asm.linesright", "false", "Show lines before opcode instead of offset");
	SETPREF ("asm.lineswide", "false", "Put a space between lines");
	SETICB ("asm.lineswidth", 7, &cb_asmlineswidth, "Number of columns for program flow arrows");
	SETICB ("asm.minvalsub", 0x100, &cb_asmminvalsub, "Minimum value to substitute in instructions (asm.varsub)");
	SETPREF ("asm.middle", "false", "Allow disassembling jumps in the middle of an instruction");
	SETPREF ("asm.noisy", "true", "Show comments considered noisy but possibly useful");
	SETPREF ("asm.offset", "true", "Show offsets at disassembly");
	SETPREF ("asm.reloff", "false", "Show relative offsets instead of absolute address in disasm");
	SETPREF ("asm.reloff.flags", "false", "Show relative offsets to flags (not only functions)");
	SETPREF ("asm.section", "false", "Show section name before offset");
	SETI ("asm.section.col", 20, "Columns width to show asm.section");
	SETCB ("asm.section.sub", "false", &cb_asmsecsub, "Show offsets in disasm prefixed with section/map name");
	SETPREF ("asm.pseudo", "false", "Enable pseudo syntax");
	SETPREF ("asm.size", "false", "Show size of opcodes in disassembly (pd)");
	SETPREF ("asm.stackptr", "false", "Show stack pointer at disassembly");
	SETPREF ("asm.cyclespace", "false", "Indent instructions depending on CPU-cycles");
	SETPREF ("asm.cycles", "false", "Show CPU-cycles taken by instruction at disassembly");
	SETI ("asm.tabs", 0, "Use tabs in disassembly");
	SETPREF ("asm.tabsonce", "false", "Only tabulate the opcode, not the arguments");
	SETI ("asm.tabsoff", 0, "tabulate spaces after the offset");
	SETPREF ("asm.trace", "false", "Show execution traces for each opcode");
	SETPREF ("asm.tracespace", "false", "Indent disassembly with trace.count information");
	SETPREF ("asm.ucase", "false", "Use uppercase syntax at disassembly");
	SETPREF ("asm.capitalize", "false", "Use camelcase at disassembly");
	SETPREF ("asm.vars", "true", "Show local function variables in disassembly");
	SETPREF ("asm.varxs", "false", "Show accesses of local variables");
	SETPREF ("asm.varsub", "true", "Substitute variables in disassembly");
	SETPREF ("asm.varsum", "false", "Show variables summary instead of full list in disasm");
	SETPREF ("asm.varsub_only", "true", "Substitute the entire variable expression with the local variable name (e.g. [local10h] instead of [ebp+local10h])");
	SETPREF ("asm.relsub", "true", "Substitute pc relative expressions in disasm");
	SETPREF ("asm.cmtfold", "false", "Fold comments, toggle with Vz");
	SETPREF ("asm.family", "false", "Show family name in disasm");
	SETPREF ("asm.symbol", "false", "Show symbol+delta instead of absolute offset");
	SETI ("asm.symbol.col", 40, "Columns width to show asm.section");
	SETCB ("asm.assembler", "", &cb_asmassembler, "Set the plugin name to use when assembling");
	SETPREF ("asm.minicols", "false", "Only show the instruction in the column disasm");
	RConfigNode *asmcpu = NODECB ("asm.cpu", R_SYS_ARCH, &cb_asmcpu);
	SETDESC (asmcpu, "Set the kind of asm.arch cpu");
	RConfigNode *asmarch = NODECB ("asm.arch", R_SYS_ARCH, &cb_asmarch);
	SETDESC (asmarch, "Set the arch to be used by asm");
	/* we need to have both asm.arch and asm.cpu defined before updating options */
	update_asmarch_options (core, asmarch);
	update_asmcpu_options (core, asmcpu);
	n = NODECB ("asm.features", "", &cb_asmfeatures);
	SETDESC (n, "Specify supported features by the target CPU");
	update_asmfeatures_options (core, n);
	SETCB ("asm.parser", "x86.pseudo", &cb_asmparser, "Set the asm parser to use");
	SETCB ("asm.segoff", "false", &cb_segoff, "Show segmented address in prompt (x86-16)");
	SETCB ("asm.decoff", "false", &cb_decoff, "Show segmented address in prompt (x86-16)");
	n = NODECB ("asm.syntax", "intel", &cb_asmsyntax);
	SETDESC (n, "Select assembly syntax");
	SETOPTIONS (n, "att", "intel", "masm", "jz", "regnum", NULL);
	SETI ("asm.nbytes", 6, "Number of bytes for each opcode at disassembly");
	SETPREF ("asm.bytespace", "false", "Separate hexadecimal bytes with a whitespace");
#if R_SYS_BITS == R_SYS_BITS_64
	SETICB ("asm.bits", 64, &cb_asmbits, "Word size in bits at assembler");
#else
	SETICB ("asm.bits", 32, &cb_asmbits, "Word size in bits at assembler");
#endif
	SETPREF ("asm.functions", "true", "Show functions in disassembly");
	SETPREF ("asm.fcncalls", "true", "Show functions calls");
	SETPREF ("asm.xrefs", "true", "Show xrefs in disassembly");
	SETPREF ("asm.demangle", "true", "Show demangled symbols in disasm");
	SETPREF ("asm.describe", "false", "Show opcode description");
	SETPREF ("asm.hints", "false", "Show hints for magic numbers in disasm");
	SETPREF ("asm.marks", "true", "Show marks before the disassembly");
	SETPREF ("asm.cmtrefs", "false", "Show flag and comments from refs in disasm");
	SETPREF ("asm.cmtpatch", "false", "Show patch comments in disasm");
	SETPREF ("asm.cmtoff", "nodup", "Show offset comment in disasm (true, false, nodup)");
	SETPREF ("asm.payloads", "false", "Show payload bytes in disasm");
	SETPREF ("asm.asciidot", "false", "Enable a char filter for string comments that passes through chars in the "
		 "range 0x20-0x7e and turns the rest into dots (except some control chars)");
	n = NODECB ("asm.strenc", "guess", &cb_asmstrenc);
	SETDESC (n, "Assumed string encoding for disasm");
	SETOPTIONS (n, "latin1", "utf8", "utf16le", "utf32le", "guess", NULL);
	SETCB ("bin.strpurge", "false", &cb_strpurge, "Try to purge false positive strings");
	SETPREF ("bin.libs", "false", "Try to load libraries after loading main binary");
	n = NODECB ("bin.strfilter", "", &cb_strfilter);
	SETDESC (n, "Filter strings");
	SETOPTIONS (n, "a", "8", "p", "e", "u", "i", "U", "f", NULL);
	SETCB ("bin.filter", "true", &cb_binfilter, "Filter symbol names to fix dupped names");
	SETCB ("bin.force", "", &cb_binforce, "Force that rbin plugin");
	SETPREF ("bin.lang", "", "Language for bin.demangle");
	SETPREF ("bin.demangle", "true", "Import demangled symbols from RBin");
	SETCB ("bin.demanglecmd", "false", &cb_bdc, "run xcrun swift-demangle and similar if available (SLOW)");

	/* bin */
	SETI ("bin.baddr", -1, "Base address of the binary");
	SETI ("bin.laddr", 0, "Base address for loading library ('*.so')");
	SETCB ("bin.dbginfo", "true", &cb_bindbginfo, "Load debug information at startup if available");
	SETPREF ("bin.relocs", "true", "Load relocs information at startup if available");
	SETICB ("bin.minstr", 0, &cb_binminstr, "Minimum string length for r_bin");
	SETICB ("bin.maxstr", 0, &cb_binmaxstr, "Maximum string length for r_bin");
	SETICB ("bin.maxstrbuf", 1024*1024*10, & cb_binmaxstrbuf, "Maximum size of range to load strings from");
	SETCB ("bin.prefix", NULL, &cb_binprefix, "Prefix all symbols/sections/relocs with a specific string");
	SETCB ("bin.rawstr", "false", &cb_rawstr, "Load strings from raw binaries");
	SETCB ("bin.strings", "true", &cb_binstrings, "Load strings from rbin on startup");
	SETCB ("bin.debase64", "false", &cb_debase64, "Try to debase64 all strings");
	SETPREF ("bin.classes", "true", "Load classes from rbin on startup");
	SETCB ("bin.verbose", "true", &cb_binverbose, "Show RBin warnings when loading binaries");

	/* prj */
	SETPREF ("prj.name", "", "Name of current project");
	SETPREF ("prj.files", "false", "Save the target binary inside the project directory");
	SETPREF ("prj.git", "false", "Every project is a git repo and saving is committing");
	SETPREF ("prj.zip", "false", "Use ZIP format for project files");
	SETPREF ("prj.gpg", "false", "TODO: Encrypt project with GnuPGv2");

	/* cfg */
	SETPREF ("cfg.plugins", "true", "Load plugins at startup");
	SETCB ("time.fmt", "%Y-%m-%d %H:%M:%S %z", &cb_cfgdatefmt, "Date format (%Y-%m-%d %H:%M:%S %z)");
	SETICB ("time.zone", 0, &cb_timezone, "Time zone, in hours relative to GMT: +2, -1,..");
	SETCB ("cfg.log", "false", &cb_cfglog, "Log changes using the T api needed for realtime syncing");
	SETPREF ("cfg.newtab", "false", "Show descriptions in command completion");
	SETCB ("cfg.debug", "false", &cb_cfgdebug, "Debugger mode");
	p = r_sys_getenv ("EDITOR");
#if __WINDOWS__ && !__CYGWIN__
	r_config_set (cfg, "cfg.editor", p? p: "notepad");
#else
	r_config_set (cfg, "cfg.editor", p? p: "vi");
#endif
	free (p);
	r_config_desc (cfg, "cfg.editor", "Select default editor program");
	SETPREF ("cfg.user", r_sys_whoami (buf), "Set current username/pid");
	SETPREF ("cfg.fortunes", "true", "If enabled show tips at start");
	SETPREF ("cfg.fortunes.type", "tips,fun", "Type of fortunes to show (tips, fun, nsfw, creepy)");
	SETPREF ("cfg.fortunes.clippy", "false", "Use ?E instead of ?e");
	SETPREF ("cfg.fortunes.tts", "false", "Speak out the fortune");
	SETI ("cfg.hashlimit", SLURP_LIMIT, "If the file is bigger than hashlimit, do not compute hashes");
	SETPREF ("cfg.prefixdump", "dump", "Filename prefix for automated dumps");
	SETCB ("cfg.sandbox", "false", &cb_cfgsanbox, "Sandbox mode disables systems and open on upper directories");
	SETPREF ("cfg.wseek", "false", "Seek after write");
	SETCB ("cfg.bigendian", "false", &cb_bigendian, "Use little (false) or big (true) endianness");

	// zign
	SETPREF ("zign.prefix", "sign", "Default prefix for zignatures matches");
	SETI ("zign.maxsz", 500, "Maximum zignature length");
	SETI ("zign.minsz", 16, "Minimum zignature length for matching");
	SETI ("zign.mincc", 10, "Minimum cyclomatic complexity for matching");
	SETPREF ("zign.graph", "true", "Use graph metrics for matching");
	SETPREF ("zign.bytes", "true", "Use bytes patterns for matching");
	SETPREF ("zign.offset", "true", "Use original offset for matching");
	SETPREF ("zign.refs", "true", "Use references for matching");

	/* diff */
	SETCB ("diff.sort", "addr", &cb_diff_sort, "Specify function diff sorting column see (e diff.sort=?)");
	SETI ("diff.from", 0, "Set source diffing address for px (uses cc command)");
	SETI ("diff.to", 0, "Set destination diffing address for px (uses cc command)");
	SETPREF ("diff.bare", "false", "Never show function names in diff output");
	SETPREF ("diff.levenstein", "false", "Use faster (and buggy) levenstein algorithm for buffer distance diffing");

	/* dir */
	SETPREF ("dir.magic", R_MAGIC_PATH, "Path to r_magic files");
#if __WINDOWS__
	SETPREF ("dir.plugins", "plugins", "Path to plugin files to be loaded at startup");
#else
	SETPREF ("dir.plugins", R2_LIBDIR"/radare2/"R2_VERSION"/", "Path to plugin files to be loaded at startup");
#endif
	SETCB ("dir.source", "", &cb_dirsrc, "Path to find source files");
	SETPREF ("dir.types", "/usr/include", "Default path to look for cparse type files");
#if __ANDROID__
	SETPREF ("dir.projects", "/data/data/org.radare.radare2installer/radare2/projects", "Default path for projects");
#elif __WINDOWS__
	SETPREF ("dir.projects", "~\\"R2_HOMEDIR"\\projects", "Default path for projects");
#else
	SETPREF ("dir.projects", "~/"R2_HOMEDIR"/projects", "Default path for projects");
#endif
	SETCB ("dir.zigns", "~/"R2_HOMEDIR"/zigns", &cb_dirzigns, "Default path for zignatures (see zo command)");
	SETPREF ("stack.bytes", "true", "Show bytes instead of words in stack");
	SETPREF ("stack.anotated", "false", "Show anotated hexdump in visual debug");
	SETI ("stack.size", 64,  "Size in bytes of stack hexdump in visual debug");
	SETI ("stack.delta", 0,  "Delta for the stack dump");

	SETCB ("dbg.libs", "", &cb_dbg_libs, "If set stop when loading matching libname");
	SETI ("dbg.hwbp", 0, "Set HW or SW breakpoints");
	SETCB ("dbg.unlibs", "", &cb_dbg_unlibs, "If set stop when unloading matching libname");
	SETPREF ("dbg.slow", "false", "Show stack and regs in visual mode in a slow but verbose mode");

	SETPREF ("dbg.bpinmaps", "true", "Force breakpoints to be inside a valid map");
	SETCB ("dbg.forks", "false", &cb_dbg_forks, "Stop execution if fork() is done (see dbg.threads)");
	n = NODECB ("dbg.btalgo", "fuzzy", &cb_dbg_btalgo);
	SETDESC (n, "Select backtrace algorithm");
	SETOPTIONS (n, "default", "fuzzy", "anal", NULL);
	SETCB ("dbg.threads", "false", &cb_stopthreads, "Stop all threads when debugger breaks (see dbg.forks)");
	SETCB ("dbg.clone", "false", &cb_dbg_clone, "Stop execution if new thread is created");
	SETCB ("dbg.aftersyscall", "true", &cb_dbg_aftersc, "Stop execution before the syscall is executed (see dcs)");
	SETCB ("dbg.execs", "false", &cb_dbg_execs, "Stop execution if new thread is created");
	SETCB ("dbg.profile", "", &cb_runprofile, "Path to RRunProfile file");
	SETCB ("dbg.args", "", &cb_dbg_args, "Set the args of the program to debug");
	SETCB ("dbg.follow.child", "false", &cb_dbg_follow_child, "Continue tracing the child process on fork. By default the parent process is traced");
	/* debug */
	SETCB ("dbg.status", "false", &cb_dbgstatus, "Set cmd.prompt to '.dr*' or '.dr*;drd;sr PC;pi 1;s-'");
#if DEBUGGER
	SETCB ("dbg.backend", "native", &cb_dbgbackend, "Select the debugger backend");
#else
	SETCB ("dbg.backend", "esil", &cb_dbgbackend, "Select the debugger backend");
#endif
	n = NODECB ("dbg.bep", "loader", &cb_dbgbep);
	SETDESC (n, "Break on entrypoint");
	SETOPTIONS (n, "loader", "entry", "constructor", "main", NULL);
	if (core->cons->rows > 30) { // HACKY
		r_config_set_i (cfg, "dbg.follow", 64);
	} else {
		r_config_set_i (cfg, "dbg.follow", 32);
	}
	r_config_desc (cfg, "dbg.follow", "Follow program counter when pc > core->offset + dbg.follow");
	SETCB ("dbg.swstep", "false", &cb_swstep, "Force use of software steps (code analysis+breakpoint)");
	SETPREF ("dbg.trace.inrange", "false", "While tracing, avoid following calls outside specified range");
	SETPREF ("dbg.trace.libs", "true", "Trace library code too");
	SETPREF ("dbg.exitkills", "true", "Kill process on exit");
	SETPREF ("dbg.exe.path", NULL, "Path to binary being debugged");
	SETCB ("dbg.consbreak", "false", &cb_consbreak, "SIGINT handle for attached processes");

	r_config_set_getter (cfg, "dbg.swstep", (RConfigCallback)__dbg_swstep_getter);

// TODO: This should be specified at first by the debug backend when attaching
#if __arm__ || __mips__
	SETICB ("dbg.bpsize", 4, &cb_dbgbpsize, "Size of software breakpoints");
#else
	SETICB ("dbg.bpsize", 1, &cb_dbgbpsize, "Size of software breakpoints");
#endif
	SETICB ("dbg.btdepth", 128, &cb_dbgbtdepth, "Depth of backtrace");
	SETCB ("dbg.trace", "false", &cb_trace, "Trace program execution (see asm.trace)");
	SETICB ("dbg.trace.tag", 0, &cb_tracetag, "Trace tag");

	/* cmd */
	char *xdotPath = r_file_path ("xdot");
	if (r_file_exists (xdotPath)) {
		r_config_set (cfg, "cmd.graph", "ag $$ > a.dot;!xdot a.dot");
	} else {
		char *dotPath = r_file_path ("dot");
		if (r_file_exists (dotPath)) {
			R_FREE (dotPath);
			char *viewer = getViewerPath();
			if (viewer) {
				char *cmd = r_str_newf ("ag $$>a.dot;!dot -Tgif -oa.gif a.dot;!%s a.gif", viewer);
				r_config_set (cfg, "cmd.graph", cmd);
				free (viewer);
				free (cmd);
			} else {
				r_config_set (cfg, "cmd.graph", "?e cannot find a valid picture viewer");
			}
		} else {
			r_config_set (cfg, "cmd.graph", "agf");
		}
		free (dotPath);
	}
	free (xdotPath);
	r_config_desc (cfg, "cmd.graph", "Command executed by 'agv' command to view graphs");
	SETPREF ("cmd.xterm", "xterm -bg black -fg gray -e", "xterm command to spawn with V@");
	SETICB ("cmd.depth", 10, &cb_cmddepth, "Maximum command depth");
	SETPREF ("cmd.bp", "", "Run when a breakpoint is hit");
	SETICB ("cmd.hitinfo", 1, &cb_debug_hitinfo, "Show info when a tracepoint/breakpoint is hit");
	SETPREF ("cmd.times", "", "Run when a command is repeated (number prefix)");
	SETPREF ("cmd.stack", "", "Command to display the stack in visual debug mode");
	SETPREF ("cmd.cprompt", "", "Column visual prompt commands");
	SETPREF ("cmd.gprompt", "", "Graph visual prompt commands");
	SETPREF ("cmd.hit", "", "Run when a search hit is found");
	SETPREF ("cmd.open", "", "Run when file is opened");
	SETCB ("cmd.log", "", &cb_cmdlog, "Every time a new T log is added run this command");
	SETPREF ("cmd.prompt", "", "Prompt commands");
	SETCB ("cmd.repeat", "false", &cb_cmdrepeat, "Empty command an alias for '..' (repeat last command)");
	SETPREF ("cmd.fcn.new", "", "Run when new function is analyzed");
	SETPREF ("cmd.fcn.delete", "", "Run when a function is deleted");
	SETPREF ("cmd.fcn.rename", "", "Run when a function is renamed");
	SETPREF ("cmd.visual", "", "Replace current print mode");
	SETPREF ("cmd.vprompt", "", "Visual prompt commands");

	SETCB ("cmd.esil.mdev", "", &cb_cmd_esil_mdev, "Command to run when memory device address is accessed");
	SETCB ("cmd.esil.intr", "", &cb_cmd_esil_intr, "Command to run when an esil interrupt happens");
	SETCB ("cmd.esil.trap", "", &cb_cmd_esil_trap, "Command to run when an esil trap happens");
	SETCB ("cmd.esil.todo", "", &cb_cmd_esil_todo, "Command to run when the esil instruction contains TODO");
	SETCB ("cmd.esil.ioer", "", &cb_cmd_esil_ioer, "Command to run when esil fails to IO (invalid read/write)");

	/* filesystem */
	n = NODECB ("fs.view", "normal", &cb_fsview);
	SETDESC (n, "Set visibility options for filesystems");
	SETOPTIONS (n, "all", "deleted", "special", NULL);

	/* hexdump */
	SETPREF ("hex.header", "true", "Show header in hexdumps");
	SETCB ("hex.pairs", "true", &cb_hexpairs, "Show bytes paired in 'px' hexdump");
	SETCB ("hex.compact", "false", &cb_hexcompact, "Show smallest 16 byte col hexdump (60 columns)");
	SETI ("hex.flagsz", 0, "If non zero, overrides the flag size in pxa");
	SETICB ("hex.cols", 16, &cb_hexcols, "Number of columns in hexdump");
	SETI ("hex.pcols", 40, "Number of pixel columns for prc");
	SETI ("hex.depth", 5, "Maximal level of recurrence while telescoping memory");
	SETPREF ("hex.onechar", "false", "Number of columns in hexdump");
	SETICB ("hex.stride", 0, &cb_hexstride, "Line stride in hexdump (default is 0)");
	SETCB ("hex.comments", "true", &cb_hexcomments, "Show comments in 'px' hexdump");

	/* http */
	SETPREF ("http.log", "true", "Show HTTP requests processed");
	SETPREF ("http.logfile", "", "Specify a log file instead of stderr for http requests");
	SETPREF ("http.cors", "false", "Enable CORS");
	SETPREF ("http.referer", "", "CSFR protection if set");
	SETPREF ("http.dirlist", "false", "Enable directory listing");
	SETPREF ("http.allow", "", "Only accept clients from the comma separated IP list");
#if __WINDOWS__
	r_config_set (cfg, "http.browser", "start");
#else
	if (r_file_exists ("/usr/bin/openURL")) { // iOS ericautils
		r_config_set (cfg, "http.browser", "/usr/bin/openURL");
	} else if (r_file_exists ("/system/bin/toolbox")) {
		r_config_set (cfg, "http.browser",
				"LD_LIBRARY_PATH=/system/lib am start -a android.intent.action.VIEW -d");
	} else if (r_file_exists ("/usr/bin/xdg-open")) {
		r_config_set (cfg, "http.browser", "xdg-open");
	} else if (r_file_exists ("/usr/bin/open")) {
		r_config_set (cfg, "http.browser", "open");
	} else {
		r_config_set (cfg, "http.browser", "firefox");
	}
	r_config_desc (cfg, "http.browser", "Command to open HTTP URLs");
#endif
	SETI ("http.maxsize", 0, "Maximum file size for upload");
	SETPREF ("http.bind", "localhost", "Server address");
	SETPREF ("http.homeroot", "~/.config/radare2/www", "http home root directory");
#if __ANDROID__
	SETPREF ("http.root", "/data/data/org.radare.radare2installer/www", "http root directory");
#elif __WINDOWS__
	SETPREF ("http.root", "www", "http root directory");
#else
	SETPREF ("http.root", R2_WWWROOT, "http root directory");
#endif
	SETPREF ("http.port", "9090", "HTTP server port");
	SETPREF ("http.maxport", "9999", "Last HTTP server port");
	SETPREF ("http.ui", "m", "Default webui (enyo, m, p, t)");
	SETPREF ("http.sandbox", "true", "Sandbox the HTTP server");
	SETI ("http.timeout", 3, "Disconnect clients after N seconds of inactivity");
	SETI ("http.dietime", 0, "Kill server after N seconds with no client");
	SETPREF ("http.verbose", "true", "Output server logs to stdout");
	SETPREF ("http.upget", "false", "/up/ answers GET requests, in addition to POST");
	SETPREF ("http.upload", "false", "Enable file uploads to /up/<filename>");
	SETPREF ("http.uri", "", "Address of HTTP proxy");
	tmpdir = r_file_tmpdir ();
	r_config_set (cfg, "http.uproot", tmpdir);
	free (tmpdir);
	r_config_desc (cfg, "http.uproot", "Path where files are uploaded");

	/* graph */
	SETPREF ("graph.comments", "true", "Show disasm comments in graph");
	SETPREF ("graph.cmtright", "false", "Show comments at right");
	SETPREF ("graph.format", "dot", "Specify output format for graphs (dot, gml, gmlfcn)");
	SETPREF ("graph.refs", "false", "Graph references in callgraphs (.agc*;aggi)");
	SETI ("graph.layout", 0, "Graph layout (0=vertical, 1=horizontal)");
	SETI ("graph.linemode", 1, "Graph edges (0=diagonal, 1=square)");
	SETPREF ("graph.font", "Courier", "Font for dot graphs");
	SETPREF ("graph.offset", "false", "Show offsets in graphs");
	SETPREF ("graph.web", "false", "Display graph in web browser (VV)");
	SETI ("graph.from", UT64_MAX, "");
	SETI ("graph.to", UT64_MAX, "");
	SETI ("graph.scroll", 5, "Scroll speed in ascii-art graph");
	SETPREF ("graph.invscroll", "false", "Invert scroll direction in ascii-art graph");
	SETPREF ("graph.title", "", "Title of the graph");
	SETPREF ("graph.gv.node", "", "Graphviz node style. (color=gray, style=filled shape=box)");
	SETPREF ("graph.gv.edge", "", "Graphviz edge style. (arrowhead=\"vee\")");
	SETPREF ("graph.gv.spline", "", "Graphviz spline style. (splines=\"ortho\")");
	SETPREF ("graph.gv.graph", "", "Graphviz global style attributes. (bgcolor=white)");
	SETPREF ("graph.gv.current", "false", "Highlight the current node in graphviz graph.");
	SETPREF ("graph.nodejmps", "true", "Enables shortcuts for every node.");
	/* hud */
	SETPREF ("hud.path", "", "Set a custom path for the HUD file");

	SETCB ("esil.exectrap", "false", &cb_exectrap, "trap when executing code in non-executable memory");
	SETCB ("esil.iotrap", "true", &cb_iotrap, "invalid read or writes produce a trap exception");
	SETPREF ("esil.romem", "false", "Set memory as read-only for ESIL");
	SETPREF ("esil.stats", "false", "Statistics from ESIL emulation stored in sdb");
	SETPREF ("esil.nonull", "false", "Prevent memory read, memory write at null pointer");
	SETCB ("esil.mdev.range", "", &cb_mdevrange, "Specify a range of memory to be handled by cmd.esil.mdev");

	/* scr */
#if __EMSCRIPTEN__
	r_config_set_cb (cfg, "scr.fgets", "true", cb_scrfgets);
#else
	r_config_set_cb (cfg, "scr.fgets", "false", cb_scrfgets);
#endif
	r_config_desc (cfg, "scr.fgets", "Use fgets() instead of dietline for prompt input");
	SETCB ("scr.echo", "false", &cb_screcho, "Show rcons output in realtime to stderr and buffer");
	SETICB ("scr.linesleep", 0, &cb_scrlinesleep, "Flush sleeping some ms in every line");
	SETICB ("scr.pagesize", 1, &cb_scrpagesize, "Flush in pages when scr.linesleep is != 0");
	SETCB ("scr.flush", "false", &cb_scrflush, "Force flush to console in realtime (breaks scripting)");
	/* TODO: rename to asm.color.ops ? */
	SETPREF ("scr.zoneflags", "true", "Show zoneflags in visual mode before the title (see fz?)");
	SETPREF ("scr.color.ops", "true", "Colorize numbers and registers in opcodes");
	SETPREF ("scr.color.bytes", "true", "Colorize bytes that represent the opcodes of the instruction");
#if __WINDOWS__ && !__CYGWIN__
	SETCB ("scr.ansicon", r_str_bool (r_cons_singleton()->ansicon),
		&scr_ansicon, "Use ANSICON mode or not on Windows");
#endif
#if __ANDROID__
	SETPREF ("scr.responsive", "true", "Auto-adjust Visual depending on screen (e.g. unset asm.bytes)");
#else
	SETPREF ("scr.responsive", "false", "Auto-adjust Visual depending on screen (e.g. unset asm.bytes)");
#endif
	SETPREF ("scr.wheelnkey", "false", "Use sn/sp and scr.nkey on wheel instead of scroll");
	SETPREF ("scr.wheel", "true", "Mouse wheel in Visual; temporaryly disable/reenable by right click/Enter)");
	SETPREF ("scr.atport", "false", "V@ starts a background http server and spawns an r2 -C");
	SETI ("scr.wheelspeed", 4, "Mouse wheel speed");
	// DEPRECATED: USES hex.cols now SETI ("scr.colpos", 80, "Column position of cmd.cprompt in visual");
	SETCB ("scr.breakword", "", &cb_scrbreakword, "Emulate console break (^C) when a word is printed (useful for pD)");
	SETCB ("scr.breaklines", "false", &cb_breaklines, "Break lines in Visual instead of truncating them");
	SETICB ("scr.columns", 0, &cb_scrcolumns, "Force console column count (width)");
	SETCB ("scr.rows", "0", &cb_scrrows, "Force console row count (height) ");
	SETICB ("scr.rows", 0, &cb_rows, "Force console row count (height) (duplicate?)");
	SETCB ("scr.fps", "false", &cb_fps, "Show FPS in Visual");
	SETICB ("scr.fix.rows", 0, &cb_fixrows, "Workaround for Linux TTY");
	SETICB ("scr.fix.columns", 0, &cb_fixcolumns, "Workaround for Prompt iOS SSH client");
	SETCB ("scr.highlight", "", &cb_scrhighlight, "Highlight that word at RCons level");
	SETCB ("scr.interactive", "true", &cb_scrint, "Start in interactive mode");
	SETI ("scr.feedback", 1, "Set visual feedback level (1=arrow on jump, 2=every key (useful for videos))");
	SETCB ("scr.html", "false", &cb_scrhtml, "Disassembly uses HTML syntax");
	n = NODECB ("scr.nkey", "flag", &cb_scrnkey);
	SETDESC (n, "Select visual seek mode (affects n/N visual commands)");
	SETOPTIONS (n, "fun", "hit", "flag", NULL);
	SETCB ("scr.pager", "", &cb_pager, "Select pager program (when output overflows the window)");
	SETPREF ("scr.randpal", "false", "Random color palete or just get the next one from 'eco'");
	SETPREF ("scr.pipecolor", "false", "Enable colors when using pipes");
	SETPREF ("scr.promptfile", "false", "Show user prompt file (used by r2 -q)");
	SETPREF ("scr.promptflag", "false", "Show flag name in the prompt");
	SETPREF ("scr.promptsect", "false", "Show section name in the prompt");
	SETPREF ("scr.tts", "false", "Use tts if available by a command (see ic)");
	SETCB ("scr.prompt", "true", &cb_scrprompt, "Show user prompt (used by r2 -q)");
	SETCB ("scr.tee", "", &cb_teefile, "Pipe output to file of this name");
	SETPREF ("scr.seek", "", "Seek to the specified address on startup");
#if __WINDOWS__ && !__CYGWIN__
	r_config_set_cb (cfg, "scr.rgbcolor", "false", &cb_rgbcolors);
#else
	r_config_set_cb (cfg, "scr.rgbcolor", "true", &cb_rgbcolors);
#endif
	r_config_desc (cfg, "scr.rgbcolor", "Use RGB colors (not available on Windows)");
	SETCB ("scr.truecolor", "false", &cb_truecolor, "Manage color palette (0: ansi 16, 1: 256, 2: 16M)");
	SETCB ("scr.color", (core->print->flags&R_PRINT_FLAGS_COLOR)?"true":"false", &cb_color, "Enable colors");
	SETCB ("scr.null", "false", &cb_scrnull, "Show no output");
	SETCB ("scr.utf8", r_cons_is_utf8()?"true":"false",
		&cb_utf8, "Show UTF-8 characters instead of ANSI");
	SETPREF ("scr.histsave", "true", "Always save history on exit");
	/* search */
	SETCB ("search.contiguous", "true", &cb_contiguous, "Accept contiguous/adjacent search hits");
	SETICB ("search.align", 0, &cb_searchalign, "Only catch aligned search hits");
	SETI ("search.chunk", 0, "Chunk size for /+ (default size is asm.bits/8");
	SETI ("search.esilcombo", 8, "Stop search after N consecutive hits");
	SETI ("search.count", 0, "Start index number at search hits");
	SETI ("search.distance", 0, "Search string distance");
	SETPREF ("search.flags", "true", "All search results are flagged, otherwise only printed");
	SETPREF ("search.overlap", "false", "Look for overlapped search hits");
	SETI ("search.maxhits", 0, "Maximum number of hits (0: no limit)");
	SETI ("search.from", -1, "Search start address");
	n = NODECB ("search.in", "file", &cb_searchin);
	SETDESC (n, "Specify search boundaries");
	SETOPTIONS (n, "raw", "block", "file", "io.maps", "io.maprange", "io.section", NULL);
	SETOPTIONS (n, "io.sections", "io.sections.write", "io.sections.exec",
				"dbg.stack", "dbg.heap", "dbg.map", "dbg.maps", "dbg.maps.exec",
				"dbg.maps.write", "anal.fcn", "anal.bb", NULL);
	SETICB ("search.kwidx", 0, &cb_search_kwidx, "Store last search index count");
	SETPREF ("search.prefix", "hit", "Prefix name in search hits label");
	SETPREF ("search.show", "true", "Show search results");
	SETI ("search.to", -1, "Search end address");

	/* rop */
	SETI ("rop.len", 5, "Maximum ROP gadget length");
	SETPREF ("rop.db", "true", "Store rop search results in sdb");
	SETPREF ("rop.subchains", "false", "Display every length gadget from rop.len=X to 2 in /Rl");
	SETPREF ("rop.conditional", "false", "Include conditional jump, calls and returns in ropsearch");
	SETPREF ("rop.nx", "false", "Include NX/XN/XD sections in ropsearch");
	SETPREF ("rop.comments", "false", "Display comments in rop search output");

	/* io */
	SETICB ("io.enforce", 0, &cb_ioenforce, "Honor IO section permissions for 1=read , 2=write, 0=none");
	SETCB ("io.buffer", "false", &cb_iobuffer, "Load and use buffer cache if enabled");
	SETCB ("io.sectonly", "false", &cb_iosectonly, "Only read from sections (if any)");
	SETI ("io.buffer.from", 0, "Lower address of buffered cache");
	SETI ("io.buffer.to", 0, "Higher address of buffered cache");
	SETCB ("io.cache", "false", &cb_iocache, "Enable cache for io changes");
	SETCB ("io.ff", "true", &cb_ioff, "Fill invalid buffers with 0xff instead of returning error");
	SETICB ("io.0xff", 0xff, &cb_io_oxff, "Use this value instead of 0xff to fill unallocated areas");
	SETCB ("io.aslr", "false", &cb_ioaslr, "Disable ASLR for spawn and such");
	SETCB ("io.va", "true", &cb_iova, "Use virtual address layout");
	SETCB ("io.pava", "false", &cb_iopava, "Use EXPERIMENTAL paddr -> vaddr address mode");
	SETCB ("io.autofd", "true", &cb_ioautofd, "Change fd when opening a new file");

	/* file */
	SETPREF ("file.desc", "", "User defined file description (used by projects)");
	SETPREF ("file.md5", "", "MD5 sum of current file");
	SETPREF ("file.info", "true", "RBin info loaded");
	SETPREF ("file.offset", "", "Offset where the file will be mapped at");
	SETCB ("file.path", "", &cb_filepath, "Path of current file");
	SETPREF ("file.lastpath", "", "Path of current file");
	SETPREF ("file.sha1", "", "SHA1 hash of current file");
	SETPREF ("file.type", "", "Type of current file");
	n = NODECB ("file.loadmethod", "fail", &cb_fileloadmethod);
	SETDESC (n, "What to do when load addresses overlap");
	SETOPTIONS (n, "fail", "overwrite", "append", NULL);
	SETI ("file.loadalign", 1024, "Alignment of load addresses");
	SETI ("file.openmany", 1, "Maximum number of files opened at once");
	SETPREF ("file.nowarn", "true", "Suppress file loading warning messages");
	SETPREF ("file.location", "", "Is the file 'local', 'remote', or 'memory'");
	/* magic */
	SETI ("magic.depth", 100, "Recursivity depth in magic description strings");

	/* rap */
	SETPREF ("rap.loop", "true", "Run rap as a forever-listening daemon");

	/* nkeys */
	SETPREF ("key.s", "", "override step into action");
	SETPREF ("key.S", "", "override step over action");
	for (i = 1; i < 13; i++) {
		snprintf (buf, sizeof (buf), "key.f%d", i);
		snprintf (buf + 10, sizeof (buf) - 10,
				"Run this when F%d key is pressed in visual mode", i);
		switch (i) {
			default: p = ""; break;
		}
		r_config_set (cfg, buf, p);
		r_config_desc (cfg, buf, buf+10);
	}

	/* zoom */
	SETCB ("zoom.byte", "h", &cb_zoombyte, "Zoom callback to calculate each byte (See pz? for help)");
	SETI ("zoom.from", 0, "Zoom start address");
	SETI ("zoom.maxsz", 512, "Zoom max size of block");
	SETI ("zoom.to", 0, "Zoom end address");

	/* lines */
	SETI ("lines.from", 0, "Start address for line seek");
	SETCB ("lines.to", "$s", &cb_linesto, "End address for line seek");
	SETCB ("lines.abs", "false", &cb_linesabs, "Enable absolute line numbers");

	r_config_lock (cfg, true);
	return true;
}
