#!/bin/bash
shopt -s expand_aliases	# Make sure aliases work in non-interactive shells
# Check if mawk exist, otherwise create alias
if ! command -v mawk &> /dev/null
then
	alias mawk='awk'
fi

# Retrieve script variables
model=$1
yr=$2

if [ -z "${model}" ] && [ -z "${year}" ]; then
	echo "Provide model and year as first and second command line parameter, respectively."
	exit
fi

if [ ${model} != "MERRA-2" ] && [ ${model} != "CESM" ] && [ ${model} != "RACMO2" ] && [ ${model} != "COSMO-2" ] && [ ${model} != "COSMO-1" ]; then
	echo "Only MERRA-2, CESM or RACMO2 supported as model."
	exit
fi

# Print Statements
echo "Working on model: ${model}"
echo "	Year ${yr}"

# Module loading and basic setup
mkdir -p log
export LD_LIBRARY_PATH=$(pwd)/usr/lib/:${LD_LIBRARY_PATH}
# module purge
# ml intel; ml proj; ml netcdf

# Create output directory
rm -r output/${model}_${yr}
mkdir -p output/${model}_${yr}

# Move into io_files dir, where computation will be peformed
cd io_files

# Retrieve directory with netCDF and create file links, if netCDF files are split by year
flag_netcdf_files_are_linked=0
grid2dpath=$(fgrep GRID2DPATH ./${model}.ini | mawk -F= '{gsub(/^[ \t]+/,"",$NF); gsub(/[ \t]+$/,"",$NF); print $NF}')	# Use gsub to remove trailing and leading white spaces
list_of_nc_files=$(find ${grid2dpath} -name "*${yr}*")
if [ ! -z "${list_of_nc_files}" ]; then
	# Create dir to link the NetCDF files
	flag_netcdf_files_are_linked=1
	rm -r ../input/${model}_${yr}
	mkdir -p ../input/${model}_${yr}
	for ncf in ${list_of_nc_files}; do
		ln -s ${ncf} ../input/${model}_${yr}/
	done
fi

# Create new ini file for each year
inifile=${model}_${yr}.ini
sed 's/..\/output\//..\/output\/'${model}'_'${yr}'\//' ${model}.ini > ${inifile}

# Set paths correctly in case we linked the netcdf files
if (( ${flag_netcdf_files_are_linked} )); then
	cp ${inifile} ${inifile}.tmp
	# Replace the paths in the [Input] section only:
	mawk -v model=${model} -v yr=${yr} '{if(/^\[/) {$0=toupper($0); if(/\[INPUT\]/) {input=1} else {input=0}}; if(input==1 && /^METEOPATH/) {print "METEOPATH = ../input/" model  "_" yr "/"} else if(input==1 && /^GRID2DPATH/) {print "GRID2DPATH = ../input/" model  "_" yr "/"} else {print $0}}' ${inifile}.tmp > ${inifile}
	rm ${inifile}.tmp
fi

# Retrieve temporal resolution from ini file:
output_res_in_seconds=$(fgrep VSTATIONS_REFRESH_RATE ${inifile} | awk -F\= '{i=$NF; gsub(/[ \t]/, "", i); print i}')	# In seconds
if [ -z "${output_res_in_seconds}" ]; then
	echo "Unable to determine temporal resolution. Is VSTATIONS_REFRESH_RATE defined in *.ini file?"
	exit
fi
output_res_in_minutes=$(echo ${output_res_in_seconds} / 60 | bc)							# In minutes

# Create .smet files for each year
if [ ${model} == "MERRA-2" ]; then
	# MERRA-2 has output each :30
	../meteoio_timeseries -b ${yr}-01-01T00:30:00 -e ${yr}-12-31T23:30:00 -c ${model}_${yr}.ini -s ${output_res_in_minutes} -p > ../log/${model}_${yr}.log 2>&1
elif [ ${model} == "COSMO-2" ]; then
	# COSMO-2 has output each hour, but at the 30min step:30
	../meteoio_timeseries -b ${yr}-01-01T00:30:00 -e ${yr}-12-31T23:30:00 -c ${model}_${yr}.ini -s ${output_res_in_minutes} -p > ../log/${model}_${yr}.log 2>&1	
else
	# Other models have output each :00, and the following line assumes the last time step is 21:00 (i.e., 3-hourly output)
	../meteoio_timeseries -b ${yr}-01-01T00:00:00 -e ${yr}-12-31T21:00:00 -c ${model}_${yr}.ini -s ${output_res_in_minutes} -p > ../log/${model}_${yr}.log 2>&1
	# ../meteoio_timeseries -b ${yr}-03-01T00:00:00 -e ${yr}-03-7T23:00:00 -c ${model}_${yr}.ini -s ${output_res_in_minutes} -p > ../log/${model}_${yr}.log 2>&1
fi
