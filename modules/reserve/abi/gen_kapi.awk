#!/usr/bin/awk -f
# gen_kapi.awk - generate abi/kapi_abi.gen.h from abi/kapi_abi.tsv.
#
#   awk -f abi/gen_kapi.awk abi/kapi_abi.tsv > abi/kapi_abi.gen.h
#
# Emits two things from the single source of truth:
#   (1) KAPI_SYMBOLS(X) - an x-macro of every unique logical symbol name, for the
#       module (disable_kapi validation/logging) and the userspace preflight.
#   (2) kapi_abi_table[] - the expected per-version signatures, guarded by
#       KAPI_ABI_WANT_TABLE so only the userspace helper pulls in the data.
#
# Version codes pack x.y.z as (x<<16)|(y<<8)|z so a single unsigned compare
# orders them; '*' since => 0 (any), '*' until => 0xffffffff (open-ended).

function vcode(s,   a) {
	if (s == "*") return "0x000000u";
	split(s, a, ".");
	return sprintf("0x%06xu", (a[1] * 65536) + (a[2] * 256) + a[3]);
}
function vcode_until(s,   a) {
	if (s == "*") return "0xffffffffu";
	split(s, a, ".");
	return sprintf("0x%06xu", (a[1] * 65536) + (a[2] * 256) + a[3]);
}

BEGIN { FS = "\t"; nsym = 0; nrow = 0 }
/^[[:space:]]*#/ { next }
/^[[:space:]]*$/ { next }
$1 == "symbol" { next }
{
	if (!($1 in seen)) { seen[$1] = 1; order[nsym++] = $1 }
	rsym[nrow] = $1; rsince[nrow] = $2; runtil[nrow] = $3;
	rkind[nrow] = $4; rret[nrow] = $5; rparams[nrow] = $6; rfeat[nrow] = $7;
	nrow++;
}
END {
	print "/* AUTO-GENERATED from abi/kapi_abi.tsv by abi/gen_kapi.awk - DO NOT EDIT. */";
	print "#ifndef KAPI_ABI_GEN_H";
	print "#define KAPI_ABI_GEN_H";
	print "";
	print "/* Every resolvable kapi logical symbol (canonical kernel name). Consumed by";
	print " * the module (disable_kapi validation/logging) and the userspace preflight,";
	print " * which must emit these exact names in the disable list. */";
	printf "#define KAPI_SYMBOLS(X)";
	for (i = 0; i < nsym; i++) printf(" \\\n\tX(%s)", order[i]);
	print "\n";
	print "#ifdef KAPI_ABI_WANT_TABLE";
	print "/* Expected ABI signature per [since, until) version range. Types are";
	print " * DESUGARED canonical (gfp_t/acr_flags_t -> uint) to mirror kCFI. */";
	print "struct kapi_abi_row {";
	print "\tconst char *sym;";
	print "\tunsigned int since;   /* >= this kver (maj<<16|min<<8|patch); 0 = any */";
	print "\tunsigned int until;   /* <  this kver; 0xffffffff = open-ended */";
	print "\tint hook;             /* 1 = vendor tracepoint: btf_trace_<sym>, +void*data */";
	print "\tconst char *ret;";
	print "\tconst char *params;   /* comma-separated; \"void\" = no params */";
	print "\tconst char *feature;";
	print "};";
	print "static const struct kapi_abi_row kapi_abi_table[] = {";
	for (i = 0; i < nrow; i++)
		printf("\t{ \"%s\", %s, %s, %d, \"%s\", \"%s\", \"%s\" },\n",
			rsym[i], vcode(rsince[i]), vcode_until(runtil[i]),
			(rkind[i] == "hook") ? 1 : 0,
			rret[i], rparams[i], rfeat[i]);
	print "};";
	print "#define KAPI_ABI_NROWS ((int)(sizeof(kapi_abi_table) / sizeof(kapi_abi_table[0])))";
	print "#endif /* KAPI_ABI_WANT_TABLE */";
	print "";
	print "#endif /* KAPI_ABI_GEN_H */";
}
