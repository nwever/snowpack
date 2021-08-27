#!/bin/bash

year_start=1996
year_end=1997
temporal_res=3600 # In seconds
model="COSMO-2"

rm -f to_exec.lst

for yr in $(seq ${year_start} ${year_end})
do
	echo "bash create_forcing.sh ${model} ${yr} ${temporal_res}" >> to_exec.lst
done
