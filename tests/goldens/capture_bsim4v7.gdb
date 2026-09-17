# Read-only capture from unmodified ngspice 47 with debug symbols.
# Usage is documented in README.md in this directory.
set debuginfod enabled off
set pagination off
set confirm off
break BSIM4v7load
run
set $golden_inst = ((BSIM4v7model*)inModel)->gen.GENinstances
set $golden_bsim = (BSIM4v7instance*)$golden_inst
set $golden_param = $golden_bsim->pParam
printf "GOLDEN pParam_BSIM4v7leff=%.17e\n", $golden_param->BSIM4v7leff
printf "GOLDEN pParam_BSIM4v7weff=%.17e\n", $golden_param->BSIM4v7weff
printf "GOLDEN pParam_BSIM4v7vth0=%.17e\n", $golden_param->BSIM4v7vth0
printf "GOLDEN pParam_BSIM4v7u0temp=%.17e\n", $golden_param->BSIM4v7u0temp
printf "GOLDEN pParam_BSIM4v7vfb=%.17e\n", $golden_param->BSIM4v7vfb
printf "GOLDEN pParam_BSIM4v7vsattemp=%.17e\n", $golden_param->BSIM4v7vsattemp
printf "GOLDEN pParam_BSIM4v7k1ox=%.17e\n", $golden_param->BSIM4v7k1ox
printf "GOLDEN pParam_BSIM4v7cdep0=%.17e\n", $golden_param->BSIM4v7cdep0
printf "GOLDEN pParam_BSIM4v7phi=%.17e\n", $golden_param->BSIM4v7phi
printf "GOLDEN here_BSIM4v7vth0=%.17e\n", $golden_bsim->BSIM4v7vth0
printf "GOLDEN here_BSIM4v7u0temp=%.17e\n", $golden_bsim->BSIM4v7u0temp
printf "GOLDEN here_BSIM4v7vfb=%.17e\n", $golden_bsim->BSIM4v7vfb
printf "GOLDEN here_BSIM4v7vsattemp=%.17e\n", $golden_bsim->BSIM4v7vsattemp
disable breakpoints
continue
quit
