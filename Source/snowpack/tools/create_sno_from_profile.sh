#!/bin/bash
# This script creates a SNOWPACK input file (*.sno) from a time step in the output *pro file.
#
# Usage: create_sno_from_profile.sh <pro-file> <date>
#  <pro file>: specify *.pro file
#  <date>    : specify date to extract. The first date equal to or after the specified date will be selected.
#
#  Output *sno file is written to stdout.
#
# NOTES:
#	- Files with soil layers are not correctly processed, since the *pro files don't contain information on
#         soil density and thermal properties.
#	- Sea ice simulations are not implemented yet, since that would require processing salinity
#	- It needs to be manually selected if pressure head needs to be written (required for simulations using
#	  Richards equation)
#	- Some parameters need manual editing of this script, like baresoil_z0, soil albedo, etc.

baresoil_z0=0.002
soilalbedo=0.09
tz=0
windscalingfactor=1.000
metamo=0		# Currently unused in SNOWPACK
write_h=0		# Write pressure head?

awk -v d=${2} -v tz=${tz} -v bsz0=${baresoil_z0} -v sab=${soilalbedo} -v wsf=${windscalingfactor} -v metamo=${metamo} -v write_h=${write_h} -F, 'BEGIN { \
	data=0; written=0; d1=sprintf("%02d.%02d.%04d %02d:%02d", substr(d,9,2), substr(d,6,2), substr(d,1,4), substr(d,12,2), substr(d,15,2)); \
} \
{ \
	if(/StationName=/) {split($1,a,"= "); station_id=a[2]; station_name=a[2];} \
	if(/Latitude/) {split($1,a,"= "); lat=a[2];} \
	if(/Longitude/) {split($1,a,"= "); lon=a[2];} \
	if(/Altitude/) {split($1,a,"= "); alt=a[2];} \
	if(/SlopeAngle/) {split($1,a,"= "); slope=a[2];} \
	if(/SlopeAzi/) {split($1,a,"= "); azi=a[2];}\
	if(data==0 && /^0504/) {if($0 ~ /mk/) {mk_in=true} else {mk_in=false; print "ERROR: mk information not present in *pro file!" > "/dev/stderr"; exit}} \
	if(/\[DATA\]/) {data=1; getline}; \
	if(data) { \
		if(/^0500/) { \
			d1=sprintf("%04d-%02d-%02dT%02d:%02d", substr($2,7,4), substr($2,4,2), substr($2,1,2), substr($2,12,2), substr($2,15,2)); \
			d2=mktime(sprintf("%04d %02d %02d %02d %02d %02d 0", substr($2,7,4), substr($2,4,2), substr($2,1,2), substr($2,12,2), substr($2,15,2), substr($2,18,2))); \
			if(d<=d1) { \
				hoar=0; \
				while(1) { \
					res=getline; \
					if(/^0501/) {nsoil=0; nsnow=0; z[0]=0; if($2==1) {nsoil=0; nsnow=0} else {for(i=3; i<=NF; i++) {if($i<0) {nsoil++} else {nsnow++}; z[i-3+(nsoil==0)]=$i/100.}}; n=nsoil+nsnow}; \
					if(/^0502/) {for(i=3; i<=NF; i++) {rho[i-3]=$i}}; \
					if(/^0503/) {for(i=3; i<=NF; i++) {T[i-3]=$i+273.15}}; \
					if(/^0504/) {for(i=3; i<=NF; i++) {mk[i-3]=$i}}; \
					if(/^0505/) {for(i=3; i<=NF; i++) {age[i-3]=$i}}; \
					if(/^0506/) {for(i=3; i<=NF; i++) {lwc[i-3]=$i/100.}}; \
					if(/^0508/) {for(i=3; i<=NF; i++) {dd[i-3]=$i}}; \
					if(/^0509/) {for(i=3; i<=NF; i++) {sp[i-3]=$i}}; \
					if(/^0511/) {for(i=3; i<=NF; i++) {rb[i-3]=$i/2.}}; \
					if(/^0512/) {for(i=3; i<=NF; i++) {rg[i-3]=$i/2.}}; \
					if(/^0514/ && $3==660) {for(i=3; i<=NF; i++) {hoar=($4/1000.)*$5}}; \
					if(/^0515/) {for(i=3; i<=NF; i++) {ice[i-3]=$i/100.}}; \
					if(/^0516/) {for(i=3; i<=NF; i++) {air[i-3]=$i/100.}}; \
					if(/^0519/) {for(i=3; i<=NF; i++) {soil[i-3]=$i/100.}}; \
					if(/^0529/) {for(i=3; i<=NF; i++) {cdot[i-3]=$i}}; \
					if(/^0500/ || res==0) { \
						printf("SMET 1.1 ASCII\n[HEADER]\nstation_id       = %s\nstation_name     = %s\nlatitude         = %s\nlongitude        = %s\naltitude         = %s\nnodata           = -999\nProfileDate      = %s\nHS_Last          = 0\nSlopeAngle       = %s\nSlopeAzi         = %s\nnSoilLayerData   = %d\nnSnowLayerData   = %s\nSoilAlbedo       = %s\nBareSoil_z0      = %s\nCanopyHeight     = 0.00\nCanopyLeafAreaIndex = 0.000000\nCanopyDirectThroughfall = 1.00\nWindScalingFactor = %s\nErosionLevel     = 0\nTimeCountDeltaHS = 0.000000\nfields           = timestamp Layer_Thick  T  Vol_Frac_I  Vol_Frac_W  Vol_Frac_V  Vol_Frac_S Rho_S Conduc_S HeatCapac_S  rg  rb  dd  sp  mk mass_hoar ne CDot metamo%s\n[DATA]\n", station_id, station_id, lat, lon, alt, d1, slope, azi, nsoil, nsnow, sab, bsz0, wsf, ((write_h)?(" h"):(""))); \
						for(i=0; i<n; i++) { \
							ts=strftime("%Y-%m-%dT%H:%M:%S", d2-age[i]*(24.*60.*60.)); \
							printf("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s%s\n", ts, z[i+1]-z[i], T[i], ice[i], lwc[i], 1.-ice[i]-lwc[i], soil[i], -999, -999, -999, rg[i], rb[i], dd[i], sp[i], mk[i], hoar, 1, cdot[i+nsoil], metamo, ((write_h)?(" -999"):(""))) \
						} \
						written=1; \
						exit; \
					} \
				} \
			} \
		} \
	} \
} END {if(!written && data) {printf("Date %s past last time stamp in file!\n", d) > "/dev/stderr"}}' $1





