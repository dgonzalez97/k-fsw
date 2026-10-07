# Sourced by the smokes: the CSP version under test, KFSW_CSP_VERSION=1 or 2
# (default 2), and what follows from it. A CSP 1 run builds into directories
# of its own, so it never reuses a CSP 2 image.
csp_profile_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../config/profiles"

case "${KFSW_CSP_VERSION:-2}" in
1)
	csp_version=1
	csp_host_bits=5
	csp_broadcast=31
	csp_suffix="-csp1"
	csp_extra_conf="$(readlink -f "$csp_profile_dir/csp-v1.conf")"
	;;
2)
	csp_version=2
	csp_host_bits=14
	csp_broadcast=16383
	csp_suffix=""
	csp_extra_conf=""
	;;
*)
	echo "ERROR: KFSW_CSP_VERSION must be 1 or 2"
	exit 1
	;;
esac

# A ';' list of extra configuration files, with this version's appended.
csp_conf_list()
{
	local list="$1"

	if [[ -n "$csp_extra_conf" ]]; then
		list="${list:+$list;}$csp_extra_conf"
	fi
	printf '%s' "$list"
}
