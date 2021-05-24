// SPDX-License-Identifier: LGPL-3.0-or-later
/***********************************************************************************/
/*  Copyright 2018 WSL Institute for Snow and Avalanche Research    SLF-DAVOS      */
/***********************************************************************************/
/* This file is part of MeteoIO.
    MeteoIO is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MeteoIO is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with MeteoIO.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <meteoio/plugins/CsvIO.h>

#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <utility>
#include <cerrno>

using namespace std;

namespace mio {
/**
 * @page csvio CsvIO
 * @section csvio_format Format
 * This plugins offers a flexible way to read Comma Separated Values (<A HREF="https://en.wikipedia.org/wiki/Comma-separated_values">CSV</A>) files. 
 * It is however assumed that:
 *     - each line contains a data record (or is an empty line)
 *     - each line contains the same number of fields;
 *     - a single character is consistently used through the file as field delimiter (to split each record into fields);
 *     - missing data are represented by an empty value, so two delimiters follow directly each other or by a special value (see NODATA in 
 * \ref csvio_metadata_extraction "Metadata extraction");
 *     - the file may contain a header that may contain additional information (metadata), see below.
 * 
 * In order to reduce the amount of manual configuration, it is possible to extract metadata from the headers or the filename, 
 * such as the station name, ID, coordinates, etc
 *
 * @section csvio_units Units
 * **The final units MUST be SI**. If not, the conversion offsets/factors must be provided to convert the data back to SI (see required keywords below)
 * or the units declared (in the headers) and supported by this plugin.
 *
 * @section csvio_keywords Keywords
 * This plugin uses the keywords described below, in the [Input] section. First, there are some general plugin options:
 * - COORDSYS: coordinate system (see Coords);
 * - COORDPARAM: extra coordinates parameters (see Coords);
 * - TIME_ZONE: the timezone that should be used to interpret the dates/times (default: 0);
 * - METEOPATH: the directory where the data files are available (mandatory);
 * - METEOPATH_RECURSIVE: if set to true, the scanning of METEOPATH is performed recursively (default: false);
 * - CSV_FILE_EXTENSION: When scanning the whole directory, look for these files (default: .csv). Note that this matching isn't restricted to the end of the file name so if you had files stat1_jan.csv, stat1_feb.csv and stat2_jan.csv you could select January's data by putting "_jan" here;
 * - CSV_SILENT_ERRORS: if set to true, lines that can not be read will be silently ignored (default: false, has priority over CSV_ERRORS_TO_NODATA);
 * - CSV_ERRORS_TO_NODATA: if true, unparseable fields (like text fields) are set to nodata, but the rest of the line is kept (default: false).
 * 
 * You can now describe the specific format for all files (prefixing the following keys by \em "CSV_") or for each particular file (prefixing the following 
 * keys by \em "CSV#_" where \em "#" represents the station index). Of course, you can mix keys that are defined for all files with some keys only defined for a 
 * few specific files (the keys defined for a particular station have priority over the global version).
 * - CSV\#_DELIMITER: field delimiter to use (default: ','), use SPACE or TAB for whitespaces (in this case, multiple whitespaces directly following each other are considered to be only one whitespace);
 * - CSV\#_NODATA: a value that should be interpreted as \em nodata (default: NAN);
 * - CSV\#_COMMENTS_MK: a single character to use as comments delimiter, everything after this char until the end of the line will be skipped (default: no comments);
 * - CSV\#_DEQUOTE: if set to true, all single and double quotes will be purged from each line \em before parsing (default: false);
 * - <b>Headers handling</b>
 *    - CSV\#_NR_HEADERS: how many lines should be treated as headers? (default: 1);
 *    - CSV\#_HEADER_DELIMITER: different field delimiter to use in header lines; optional
 *    - CSV\#_HEADER_REPEAT_MK: a string that is used to signal another copy of the headers mixed with the data in the file (the matching is done anywhere in the line) (default: empty);
 *    - CSV\#_UNITS_HEADERS: header line providing the measurements units (the subset of recognized units is small, please inform us if one is missing for you); optional
 *    - CSV\#_UNITS_OFFSET: offset to add to each value in order to convert it to SI; optional
 *    - CSV\#_UNITS_MULTIPLIER: factor to multiply each value by, in order to convert it to SI; optional
 * - <b>Fields parsing</b>
 *    - CSV\#_COLUMNS_HEADERS: header line to interpret as columns headers (default: 1, see also \ref csvio_special_fields "special field names");
 *    - CSV\#_FIELDS: one line providing the columns headers (if they don't exist in the file or to overwrite them). If a field is declared as "ID" then only the lines that have the proper ID for the current station will be kept; if a field is declared as "SKIP" it will be skipped; otherwise date/time parsing fields are supported according to <i>Date/Time parsing</i> below (see also the \ref csvio_special_fields "special field names" for more); optional
 *    - CSV\#_FILTER_ID: if the data contains an "ID" column, which ID should be kept (all others will be rejected); default: station ID
 *    - CSV\#_UNITS: one line providing space delimited units for each column (including the timestamp), no units is represented as "-". This is an alternative to relying on a units line in the file itself or relying on units_offset / units_multiplier. Please keep in mind that the choice of recognized units is very limited... (C, degC, cm, in, ft, F, deg, pc, % and a few others)
 *    - CSV\#_SKIP_FIELDS: a space-delimited list of field to skip (first field is numbered 1). Keep in mind that when using parameters such as UNITS_OFFSET, the skipped field MUST be taken into consideration (since even if a field is skipped, it is still present in the file!); optional
 *    - CSV\#_SINGLE_PARAM_INDEX: if the parameter is identified by {PARAM} (see below), this sets the column number in which the parameter is found; optional
 * - <b>Date/Time parsing</b>. There are several possibilities: the date/time is provided as one or two strings; as a purely decimal number following a given representation; as each component as a separate column.
 *    - Date/Time as string(s):
 *       - CSV\#_DATETIME_SPEC: mixed date and time format specification (defaultis ISO_8601: YYYY-MM-DDTHH24:MI:SS);
 *       - CSV\#_DATE_SPEC: date format specification (default: YYYY_MM_DD);
 *       - CSV\#_TIME_SPEC: time format specification (default: HH24:MI:SS);
 *    - Date/Time decimal representation:
 *       - CSV\#_DECIMALDATE_TYPE: the numerical representation that is used, one of EXCEL, JULIAN, MJULIAN, MATLAB, RFC868 or UNIX (see \ref decimal_date_representation "decimal date representations");
 *    - Date/Time as separate components: 
 *       - the fields must be named (either from the headers or through the CSV\#_FIELDS key) as YEAR, JDAY (number of days since the begining of the year), MONTH, DAY, NTIME (numerical representation of time, for example 952 for 09:52), HOURS, MINUTES, SECONDS (if minutes or seconds are missing, they will be assumed to be zero). See \ref csvio_special_fields "special field names" for accepted synonyms;
 *       - if/when no year component is provided, it is possible to define a fallback year with the CSV\#_FALLBACK_YEAR key;
 *       - when using CSV\#_FALLBACK_YEAR, it will by default assume that all data for times greater than 1st October that appear 
 * before data belonging to times before 1st of October are actually data from the year before. Please set CSV\#_FALLBACK_AUTO_WRAP to false if this is not desired.
 * - <b>Metadata</b>
 *    - CSV\#_NAME: the station name to use (if provided, has priority over the special headers);
 *    - CSV\#_ID: the station id to use (if provided, has priority over the special headers);
 *    - CSV\#_SLOPE: the slope angle in degrees at the station (if providing a slope, also provide an azimuth);
 *    - CSV\#_AZIMUTH: the slope azimuth in degrees from North at the station ;
 *    - CSV\#_SPECIAL_HEADERS: description of how to extract more metadata out of the headers; optional
 *    - CSV\#_FILENAME_SPEC: pattern to parse the filename and extract metadata out of it; optional
 *    - The following two keys provide mandatory data for each station, therefore there is no "global" version and they must be defined:
 *       - STATION\#: input filename (in METEOPATH). As many meteofiles as needed may be specified. If nothing is specified, the METEOPATH directory will be scanned for files with the extension specified in CSV_FILE_EXTENSION;
 *       - POSITION\#: coordinates of the station (default: reading key "POSITION", see \link Coords::Coords(const std::string& in_coordinatesystem, const std::string& in_parameters, std::string coord_spec) Coords()\endlink for the syntax). This key can only be omitted if lat/lon/altitude are provided in the file name or in the headers (see CSV\#_FILENAME_SPEC and CSV\#_SPECIAL_HEADERS);
 *
 * If no ID has been provided, an automatic station ID will be generated as "ID{n}" where *n* is the current station's index. Regarding the units handling, 
 * it is only performed through either the CSV_UNITS_OFFSET key or the CSV_UNITS_OFFSET / CSV_UNITS_MULTIPLIER keys. These keys expect a value for each
 * column of the file, including the date and time.
 * 
 * @subsection csvio_special_fields Special field names
 * When reading the field names, either from a file header or as provided in the configuration file with the CSV\#_FIELDS key, the fields will be attributed to 
 * variables bearing the same names. But some field names will be recognized and automatically interpreted as either known internal 
 * parameter names (see \ref meteoparam "this list") or date/time parameters or stationID the data belongs to. Besides MeteoIO's internal parameter names, the 
 * following field names are also automatically recognized (synonyms are separated by ',' while different parameters are separated by ';'):
 *    - TIMESTAMP, TS, DATETIME; DATE; TIME; YEAR; JDAY, JDN, YDAY, DAY_OF_YEAR, DOY; MONTH; DAY; NTIME; HOUR, HOURS; MINUTE, MINUTES; SECOND, SECONDS; ID, STATIONID;
 *    - TEMPERATURE_AIR, AIRTEMP; SOIL_TEMPERATURE, SOILTEMP; PRECIPITATION, PREC; REFLECTED_RADIATION; INCOMING_RADIATION, INCOMINGSHORTWAVERADIATION; WIND_DIRE
CTION, WD; RELATIVE_HUMIDITY, RELATIVEHUMIDITY; WIND_VELOCITY, WS; PRESSURE, STATIONPRESSURE; INCOMING_LONGWAVE, INCOMINGLONGWAVERADIATION; SNOWSURFACETEMPERATURE; WS_MAX;
 * 
 * @note Since most parameter won't have names that are recognized by MeteoIO, it is advised to map them to \ref meteoparam "MeteoIO's internal names". 
 * This is done either by using the CSV_FIELDS key or using the EditingMove feature of the 
 * \ref data_editing "Input Data Editing" stage.
 * 
 * @section csvio_date_specs Date and time specification
 * In order to be able to read any date and time format, the format has to be provided in the configuration file. This is provided as a string containing
 * the following special markers:
 * - YYYY: the 4 digits year;
 * - MM: the two digits month;
 * - DD: the two digits day;
 * - HH24: the two digits hour of the day (0-24);
 * - MI: the two digits minutes (0-59);
 * - SS: the number of seconds (0-59.98), that can be decimal;
 * - TZ: the numerical timezone as offset to GMT (see note below).
 *
 * Any other character is interpreted as itself, present in the string. It is possible to either provide a combined datetime field (so date and time are combined into
 * one single field) or date and time as two different fields. For example:
 * - YYYY-MM-DDTHH24:MI:SS described an <A HREF="https://en.wikipedia.org/wiki/ISO_8601">ISO 8601</A> datetime field;
 * - MM/DD/YYYY described an anglo-saxon date;
 * - DD.MM.YYYY HH24:MI:SS is for a Swiss formatted datetime.
 * 
 * @note When providing a timezone field, it \em must appear at the end of the string. it can either be numerical (such as "+1.") or an abbreviation
 * such as "CET" (see https://en.wikipedia.org/wiki/List_of_time_zone_abbreviations).
 * 
 * When this plugin identifies the fields by their column headers, it will look for TIMESTAMP or DATETIME for a combined date and time field, or DATE or TIME for (respectively) a
 * date and time field. Usually, other labels will not be recognized.
 * 
 * @section csvio_metadata_extraction Metadata extraction
 * Since there is no unified way of providing metadata (such as the location, station name, etc) in CSV files, this information has to
 * be either provided in the configuration file (see \ref csvio_keywords "Configuration keywords") or extracted out of either the file
 * name or the file headers. A specific syntax allows to describe where to find which metadata field type.
 * 
 * @subsection csvio_metadata_field_types Metadata fields types
 * The following field types are supported:
 * - NAME;
 * - ID (this will be used as a handle for the station);
 * - ALT (for the altitude);
 * - LON (for the longitude);
 * - LAT (for the latitude);
 * - EASTING (as per your input coordinate system);
 * - NORTHING (if LON/LAT is not used);
 * - SLOPE (in degrees);
 * - AZI (for the slope azimuth, in degree as read from a compass);
 * - NODATA (string to interpret as nodata);
 * - PARAM (to identify the content of a file that only contains the time information and one meteorological parameter);
 * - SKIP (skip this field).
 * 
 * If ID or NAME appear more than once in one specification string, their mutliple values will be appended.
 *
 * @subsection csvio_special_headers Header metadata extraction
 * This is performed with the "CSV#_SPECIAL_HEADERS" configuration key. This key is followed by as many metadata 
 * specifications as necessary, of the form {field}:{line}:{column}.
 *
 * Therefore, if the station name is available on line 1, column 3 and the station id on line 2, column 5, the configuration would be:
 * @code
 * CSV_SPECIAL_HEADERS = name:1:3 id:2:5
 * @endcode
 * 
 * @subsection csvio_filename_parsing Filename metadata extraction
 * This is performed with the "CSV#_FILENAME_SPEC" configuration key. This key is followed by the metadata specification 
 * that will be applied to identify the information to extract as well as substrings that are used as "markers" delimiting 
 * the different fields (enclosed within {}).
 * 
 * For example, to parse the filename "H0118_Generoso-Calmasino_-_Precipitation.csv" use (please note that the extension is NOT provided):
 * @code
 * CSV_FILENAME_SPEC = {ID}_{NAME}-{SKIP}_-_{PARAM}
 * @endcode
 * 
 * If the CSV_FIELDS key is also present, it will have priority. Therefore, it is possible to define one CSV_FILENAME_SPEC for several files and 
 * only define CSV\#_FIELDS for the files that would require a different handling (for example because their parameter would not be recognized).
 * Moreover, it is possible to set \em "AUTOMERGE" to "true" in the [InputEditing] section, so all files leading to the same station ID will be merged together
 * into one single station.
 * 
 * @note Obviously, the {PARAM} metadata field type can only be used for files that contain the time information (either as datetime or seperate date and time) and one
 * meteorological parameter. If there are multiple (potentially unimportant) parameters in your file you have to set CSV_SINGLE_PARAM_INDEX to the column number
 * matching your parameter.
 * 
 * @section csvio_examples Examples
 * In order to read a bulletin file downloaded from IDAWEB, you need the following configuration:
 * @code
 * METEO = CSV
 * METEOPATH = ./input/meteo
 * CSV_DELIMITER = SPACE
 * CSV_NR_HEADERS = 1
 * CSV_COLUMNS_HEADERS = 1
 * CSV_DATETIME_SPEC = YYYYMMDDHH24
 * CSV_NODATA = -
 * 
 * STATION1 = IDA_station1.csv
 * POSITION1 = latlon (46.80284, 9.77726, 2418)
 * CSV1_NAME = TEST
 * CSV1_ID = myID
 * CSV1_FIELDS = SKIP TIMESTAMP HS RSWR TA SKIP SKIP RH SKIP SKIP ILWR
 * CSV1_UNITS_OFFSET = 0 0 0 0 273.15 0 0 0 0 0 0
 * CSV1_UNITS_MULTIPLIER = 1 1 0.01 1 1 1 1 0.01 1 1 1
 * @endcode
 * 
 * In order to read a CSV file produced by a Campbell data logger with Swiss-formatted timestamps, you need the following configuration:
 * @code
 * METEO = CSV
 * METEOPATH = ./input/meteo
 * CSV_NR_HEADERS = 4
 * CSV_COLUMNS_HEADERS = 2
 * CSV_UNITS_HEADERS = 3
 * CSV_DATETIME_SPEC = DD.MM.YYYY HH24:MI:SS
 * CSV_SPECIAL_HEADERS = name:1:2 id:1:4
 * 
 * STATION1 = DisMa_DisEx.csv
 * POSITION1 = latlon 46.810325 9.806657 2060
 * CSV1_ID = DIS4
 * @endcode
 *
 * In order to read a set of files each containing only one parameter and merge them together (see \ref data_editing "input data editing" for more
 * on the merge feature), extracting the station ID, name and meteorological parameter from the filename:
 *@code
 * [Input]
 * METEO = CSV
 * METEOPATH = ./input/meteo
 * CSV_DELIMITER = ;
 * CSV_HEADER_LINES = 1
 * CSV_DATE_SPEC = DD/MM/YYYY
 * CSV_TIME_SPEC = HH24:MI
 * POSITION = latlon (46.8, 9.80, 1700)
 * CSV_FILENAME_SPEC = {ID}_{NAME}_-_{SKIP}-{PARAM}
 * CSV_COLUMNS_HEADERS = 1
 *
 * STATION1 = H0118_Generoso_-_Calmasino_precipitation.csv
 *
 * STATION2 = H0118_Generoso_-_Calmasino_temperature.csv    #the parameter name is ambiguous, it will not be recognized
 * CSV2_FIELDS = DATE TIME TA                               #so we define the parameter manually
 * CSV2_UNITS_OFFSET = 0 0 273.15
 *
 * STATION3 = H0118_Generoso_-_Calmasino_reflected_solar_radiation.csv
 * STATION4 = H0118_Generoso_-_Calmasino_relative_humidity.csv
 * STATION5 = H0118_Generoso_-_Calmasino_wind_velocity.csv
 *
 * [InputEditing]
 * AUTOMERGE = true
 * @endcode
 * 
 * In order to read a set of files and merge them together (see \ref data_editing "input data editing" for more
 * on the merge feature):
 *@code
 * [Input]
 * METEO = CSV
 * METEOPATH = ./input/meteo
 * CSV_DELIMITER = ;
 * CSV_HEADER_LINES = 1
 * CSV_DATE_SPEC = DD/MM/YYYY
 * CSV_TIME_SPEC = HH24:MI
 * POSITION = latlon (46.8, 9.80, 1700)
 * CSV_NAME = Generoso
 *
 * CSV1_FIELDS = DATE TIME PSUM HS
 * STATION1 = H0118_lg23456.csv
 *
 * CSV2_FIELDS = DATE TIME TA
 * STATION2 = H0118_lg7850.csv
 * CSV2_UNITS_OFFSET = 0 0 273.15
 *
 * CSV3_FIELDS = DATE TIME RSWR ISWR
 * STATION3 = H0118_lg64520.csv
 *
 * CSV4_FIELDS = DATE TIME RH
 * STATION4 = H0118_lg45302.csv
 * CSV4_UNITS_MULTIPLIER = 1 1 0.01
 *
 * CSV5_FIELDS = DATE TIME VW
 * STATION5 = H0118_wind_velocity.csv
 *
 * [InputEditing]
 * ID1::MERGE = ID2 ID3 ID4 ID5
 * @endcode
 * 
 * When reading a file containing the data from multiple stations, each line containing the station ID it applies to, it is necessary
 * to provide the requested station ID for each new station as well as declare which is the ID field (if the headers declare an "ID" column or
 * a "STATIONID" column, this will also work)
 * @code
 * METEO = CSV
 * METEOPATH = ./input
 * CSV_DELIMITER = ,   
 * CSV_NR_HEADERS = 1
 * CSV_COLUMNS_HEADERS = 1
 * CSV_DATETIME_SPEC = YYYY-MM-DD HH24:MI:SS
 * CSV_FIELDS = ID TIMESTAMP SKIP TA RH DW VW SKIP HS SKIP SKIP P SKIP SKIP SKIP ISWR SKIP SKIP SKIP TSG SKIP SKIP ISWR ILWR TSS
 * CSV_UNITS_OFFSET = 0 0 0 273.15 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 273.15
 * CSV_UNITS_MULTIPLIER = 1 1 1 1 0.01 1 1 1 0.01 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
 * 
 * STATION1 = Extracted_data.csv
 * POSITION1 = latlon (46.8, 9.81, 1511.826)
 * CSV1_ID = 109
 * CSV1_NAME = Station109
 * 
 * STATION2 = Extracted_data.csv
 * POSITION2 = xy (45.8018, 9.82, 111.826)
 * CSV2_ID = 105
 * CSV2_NAME = Station105
 * @endcode
 */

//helper function to sort the static keys used for specifying the date/time formats
inline bool sort_dateKeys(const std::pair<size_t,size_t> &left, const std::pair<size_t,size_t> &right) { return left.first < right.first;}

void CsvDateTime::updateMaxCol() 
{
	if (decimal_date!=IOUtils::npos && decimal_date>max_dt_col) max_dt_col=decimal_date;
	if (date_str!=IOUtils::npos && date_str>max_dt_col) max_dt_col=date_str;
	if (time_str!=IOUtils::npos && time_str>max_dt_col) max_dt_col=time_str;
	if (year!=IOUtils::npos && year>max_dt_col) max_dt_col=year;
	if (jdn!=IOUtils::npos && jdn>max_dt_col) max_dt_col=jdn;
	if (month!=IOUtils::npos && month>max_dt_col) max_dt_col=month;
	if (day!=IOUtils::npos && day>max_dt_col) max_dt_col=day;
	if (time!=IOUtils::npos && time>max_dt_col) max_dt_col=time;
	if (hours!=IOUtils::npos && hours>max_dt_col) max_dt_col=hours;
	if (minutes!=IOUtils::npos && minutes>max_dt_col) max_dt_col=minutes;
	if (seconds!=IOUtils::npos && seconds>max_dt_col) max_dt_col=seconds;
}

int CsvDateTime::getFixedYear(const double& i_jdn)
{
	if (i_jdn<273.) auto_wrap = false;
	if (auto_wrap) return year_cst - 1;
	return year_cst;
}

int CsvDateTime::getFixedYear(const int& i_month)
{
	if (i_month<10) auto_wrap = false;
	if (auto_wrap) return year_cst - 1;
	return year_cst;
}

bool CsvDateTime::isSet() const 
{
	//date and time strings
	if (date_str!=IOUtils::npos && time_str!=IOUtils::npos) return true;
	if (decimal_date!=IOUtils::npos) return true;
	
	const bool components_time = (time!=IOUtils::npos || hours!=IOUtils::npos);
	const bool components_date = ((year!=IOUtils::npos || year_cst!=IOUtils::inodata) && (jdn!=IOUtils::npos || (month!=IOUtils::npos && day!=IOUtils::npos)));
	
	//date string and components time
	//if (date_str!=IOUtils::npos && components_time) return true;
	
	//components date and time string
	//if (components_date && time_str!=IOUtils::npos) return true;
	
	if (components_date && components_time) return true;
	return false;
}

std::string CsvDateTime::toString() const 
{
	std::ostringstream os;
	os << "[";
	if (decimal_date!=IOUtils::npos) os << "decimal_date→" << decimal_date << " ";
	if (date_str!=IOUtils::npos) os << "date_str→" << date_str << " ";
	if (time_str!=IOUtils::npos) os << "time_str→" << time_str << " ";
	if (year!=IOUtils::npos) os << "year→" << year << " ";
	if (year_cst!=IOUtils::nodata) os << "year_cst→" << year << " ";
	if (jdn!=IOUtils::npos) os << "jdn→" << jdn << " ";
	if (month!=IOUtils::npos) os << "month→" << month << " ";
	if (day!=IOUtils::npos) os << "day→" << day << " ";
	if (time!=IOUtils::npos) os << "time_num→" << time << " ";
	if (hours!=IOUtils::npos) os << "hours→" << hours << " ";
	if (minutes!=IOUtils::npos) os << "minutes→" << minutes << " ";
	if (seconds!=IOUtils::npos) os << "seconds→" << seconds << " ";
	if (auto_wrap) os << "auto_wrap";
	os << "]";
	return os.str();
}

///////////////////////////////////////////////////// Start of the CsvParameters class //////////////////////////////////////////

//parse the user provided special headers specification. It is stored as <line_nr, <column, field_type>> in a multimap
//(since there can be multiple keys on the same line)
std::multimap< size_t, std::pair<size_t, std::string> > CsvParameters::parseHeadersSpecs(const std::vector<std::string>& vecMetaSpec)
{
	std::multimap< size_t, std::pair<size_t, std::string> > meta_spec;
	for (size_t ii=0; ii<vecMetaSpec.size(); ii++) {
		std::vector<std::string> vecArgs;
		if (IOUtils::readLineToVec(vecMetaSpec[ii], vecArgs, ':') !=3)
			throw InvalidFormatException("Wrong format for Metadata specification '"+vecMetaSpec[ii]+"'", AT);
		const int linenr = atoi( vecArgs[1].c_str() );
		const int colnr = atoi( vecArgs[2].c_str() );
		if (linenr<=0 || colnr<=0)
			throw InvalidFormatException("Line numbers and column numbers must be >0 in Metadata specification '"+vecMetaSpec[ii]+"'", AT);
		
		meta_spec.insert( make_pair( linenr, make_pair( colnr, vecArgs[0]) ) );
	}
	
	return meta_spec;
}

//Given a list of fields to skip, fill the skip_fields map
void CsvParameters::setSkipFields(const std::vector<size_t>& vecSkipFields)
{
	for (size_t ii=0; ii<vecSkipFields.size(); ii++) {
		if (vecSkipFields[ii]==0)
			throw InvalidArgumentException("Wrong format specification for fields to skip: first field is numbered field 1", AT);
		
		skip_fields[ vecSkipFields[ii]-1 ] = true;
	}
}

void CsvParameters::setDelimiter(const std::string& delim)
{
	if (delim.size()==1) {
		csv_delim = delim[0];
	} else {
		if (delim.compare("SPACE")==0 || delim.compare("TAB")==0) 
			csv_delim=' ';
		else 
			throw InvalidArgumentException("The CSV delimiter must be a single character or SPACE or TAB", AT);
	}
}

void CsvParameters::setHeaderDelimiter(const std::string& delim)
{
	if (delim.size()==1) {
		header_delim = delim[0];
	} else {
		if (delim.compare("SPACE")==0 || delim.compare("TAB")==0)
			header_delim=' ';
		else
			throw InvalidArgumentException("The CSV header delimiter must be a single character or SPACE or TAB", AT);
	}
}

std::string CsvParameters::identifyField(const std::string& fieldname)
{
	if (fieldname.compare(0, 15, "TEMPERATURE_AIR")==0 || fieldname.compare(0, 7, "AIRTEMP")==0 || fieldname.compare(0, 16, "TEMPERATURA_ARIA")==0) return "TA";
	else if (fieldname.compare(0, 16, "SOIL_TEMPERATURE")==0 || fieldname.compare(0, 8, "SOILTEMP")==0) return "TSG";
	else if (fieldname.compare(0, 13, "PRECIPITATION")==0 || fieldname.compare(0, 4, "PREC")==0 || fieldname.compare(0, 14, "PRECIPITAZIONE")==0) return "PSUM";
	else if (fieldname.compare(0, 19, "REFLECTED_RADIATION")==0 || fieldname.compare(0, 26, "RADIAZIONE_SOLARE_RIFLESSA")==0) return "RSWR";
	else if (fieldname.compare(0, 18, "INCOMING_RADIATION")==0 || fieldname.compare(0, 26, "INCOMINGSHORTWAVERADIATION")==0 || fieldname.compare(0, 27, "RADIAZIONE_SOLARE_INCIDENTE")==0) return "RSWR";
	else if (fieldname.compare(0, 14, "WIND_DIRECTION")==0 || fieldname.compare(0, 2, "WD")==0 || fieldname.compare(0, 15, "DIREZIONE_VENTO")==0) return "DW";
	else if (fieldname.compare(0, 17, "RELATIVE_HUMIDITY")==0 || fieldname.compare(0, 16, "RELATIVEHUMIDITY")==0 || fieldname.compare(0, 15, "UMIDIT_RELATIVA")==0) return "RH";
	else if (fieldname.compare(0, 13, "WIND_VELOCITY")==0 || fieldname.compare(0, 2, "WS")==0 || fieldname.compare(0, 13, "VELOCIT_VENTO")==0) return "VW";
	else if (fieldname.compare(0, 8, "PRESSURE")==0 || fieldname.compare(0, 15, "STATIONPRESSURE")==0) return "P";
	else if (fieldname.compare(0, 17, "INCOMING_LONGWAVE")==0 || fieldname.compare(0, 25, "INCOMINGLONGWAVERADIATION")==0) return "ILWR";
	else if (fieldname.compare(0, 22, "SNOWSURFACETEMPERATURE")==0 ) return "TSS";
	else if (fieldname.compare(0, 6, "WS_MAX")==0) return "VW_MAX";
	
	return fieldname;
}

//Given a provided field_type, attribute the value to the proper metadata variable.
void CsvParameters::assignMetadataVariable(const std::string& field_type, const std::string& field_val, double &lat, double &lon, double &easting, double &northing)
{
	if (field_type=="ID") {
		if (id.empty()) id = field_val;
	} else if (field_type=="NAME") {
		if (name.empty()) name = field_val;
	} else if (field_type=="NODATA") {
			nodata = field_val;
	} else if (field_type=="SKIP") {
		return;
	} else if (field_type=="PARAM") {
		std::string param( IOUtils::strToUpper( field_val ) );
		if (MeteoData::getStaticParameterIndex( param )!=IOUtils::npos) {
			single_field = param;
			return;
		}
		
		IOUtils::replaceInvalidChars(param); //remove accentuated characters, etc
		param = identifyField( param ); //try to map non-standard names to mio's names
		
		single_field = param;
	} else {
		if (field_type=="ALT" || field_type=="LON" || field_type=="LAT" || field_type=="SLOPE" || field_type=="AZI" || field_type=="EASTING" || field_type=="NORTHING") {
			double tmp;
			if (!IOUtils::convertString(tmp, field_val))
				throw InvalidArgumentException("Could not extract metadata '"+field_type+"' for "+file_and_path, AT);
			
			if (field_type=="ALT") location.setAltitude( tmp, false);
			if (field_type=="LON") lon = tmp;
			if (field_type=="LAT") lat = tmp;
			if (field_type=="SLOPE") slope = tmp;
			if (field_type=="AZI") azi = tmp;
			if (field_type=="EASTING") easting = tmp;
			if (field_type=="NORTHING") northing = tmp;
		} else 
			throw InvalidFormatException("Unknown parsing key '"+field_type+"' when extracting metadata", AT);
	}
}

//Using the special headers parsed specification (done in parseHeadersSpecs), some metadata is extracted from the headers
void CsvParameters::parseSpecialHeaders(const std::string& line, const size_t& linenr, const std::multimap< size_t, std::pair<size_t, std::string> >& meta_spec, double &lat, double &lon, double &easting, double &northing)
{
	std::vector<std::string> vecStr;
	IOUtils::readLineToVec(line, vecStr, header_delim);
	
	const bool readID = (id.empty()); //if the user defined CSV_ID, it has priority
	const bool readName = (name.empty()); //if the user defined CSV_NAME, it has priority
	std::string prev_ID, prev_NAME;
	std::multimap<size_t, std::pair<size_t, std::string> >::const_iterator it;
	for (it=meta_spec.equal_range(linenr).first; it!=meta_spec.equal_range(linenr).second; ++it) {
		const size_t colnr = (*it).second.first;
		const std::string field_type( IOUtils::strToUpper( (*it).second.second) );
		if (colnr>vecStr.size() || colnr==0)
			throw InvalidArgumentException("Metadata specification for '"+field_type+"' refers to a non-existent field for file (either 0 or too large) "+file_and_path, AT);
		
		//remove the quotes from the field
		std::string field_val( vecStr[colnr-1] );
		IOUtils::removeQuotes(field_val);
		
		//we handle ID and NAME differently in order to support appending
		if (field_type=="ID" && readID) {
			id = prev_ID+field_val;
			prev_ID = id+"-";
		} else if (field_type=="NAME" && readName) {
			name = prev_NAME+field_val;
			prev_NAME = name+"-";
		} else {
			assignMetadataVariable(field_type, field_val, lat, lon, easting, northing);
		}
	}
}

//Extract metadata from the filename, according to a user-provided specification.
//filename_spec in the form of {ID}_{NAME}-{PARAM}_-_{SKIP} where {ID} is a variable and '_-_' is a constant pattern
void CsvParameters::parseFileName(std::string filename, const std::string& filename_spec, double &lat, double &lon, double &easting, double &northing)
{
	filename = FileUtils::removeExtension( FileUtils::getFilename(filename) );
	size_t pos_fn = 0, pos_mt = 0; //current position in the filename and in the filename_spec
	if (filename_spec[0]!='{') { //there is a constant pattern at the beginning, getting rid of it
		const size_t start_var = filename_spec.find('{');
		if (start_var==std::string::npos) throw InvalidFormatException("No variables defined for filename parsing", AT);
		const std::string pattern( filename_spec.substr(0, start_var) );
		if (filename.substr(0, start_var)!=pattern) throw InvalidFormatException("The filename pattern '"+filename_spec+"' does not match with the given filename ('"+filename+"') for metadata extraction", AT);
		pos_mt = start_var;
		pos_fn = start_var;
	}

	const bool readID = (id.empty()); //if the user defined CSV_ID, it has priority
	const bool readName = (name.empty()); //if the user defined CSV_NAME, it has priority
	std::string prev_ID, prev_NAME;
	//we now assume that we start with a variable
	do {
		//the start of the next constant pattern defines the end of the current variable
		const size_t start_pattern = filename_spec.find('}', pos_mt);
		const size_t end_pattern = filename_spec.find('{', pos_mt+1);
		if (start_pattern==std::string::npos) {
			if (end_pattern!=std::string::npos) throw InvalidFormatException("Unclosed variable delimiter '}' in filename parsing", AT);
			break; //no more variables to read
		}
		const size_t pattern_len = end_pattern - start_pattern - 1;
		
		size_t len_var = std::string::npos; //until end of string
		if (end_pattern!=std::string::npos) {
			const std::string pattern = filename_spec.substr(start_pattern+1, pattern_len); //skip } and {
			const size_t pos_pattern_fn = filename.find(pattern, pos_fn);
			if (pos_pattern_fn==std::string::npos) throw InvalidFormatException("The filename pattern '"+filename_spec+"' does not match with the given filename ('"+filename+"') for metadata extraction", AT);
			len_var = pos_pattern_fn - pos_fn;
		}

		//read the variable type and value
		const std::string field_type( IOUtils::strToUpper(filename_spec.substr(pos_mt+1, start_pattern-pos_mt-1)) ); //skip { and }
		const std::string value( filename.substr(pos_fn, len_var) );
		//we handle ID and NAME differently in order to support appending
		if (field_type=="ID" && readID) {
			id = prev_ID+value;
			prev_ID = id+"-";
		} else if (field_type=="NAME" && readName) {
			name = prev_NAME+value;
			prev_NAME = name+"-";
		} else {
			assignMetadataVariable(field_type, value, lat, lon, easting, northing);
		}

		if (end_pattern==std::string::npos) break; //nothing more to parse
		pos_mt = end_pattern;
		pos_fn = pos_fn + len_var + pattern_len;
	} while (true);

}

//user provided field names are in fieldNames, header field names are in headerFields
//and user provided fields have priority.
void CsvParameters::parseFields(const std::vector<std::string>& headerFields, std::vector<std::string>& fieldNames)
{
	const bool user_provided_field_names = (!fieldNames.empty());
	if (headerFields.empty() && !user_provided_field_names)
		throw InvalidArgumentException("No columns names could be found. Please either provide CSV_COLUMNS_HEADERS or CSV_FIELDS", AT);
	
	if (!user_provided_field_names) fieldNames = headerFields;
	for (size_t ii=0; ii<fieldNames.size(); ii++) {
		std::string &tmp = fieldNames[ii];
		IOUtils::trim( tmp ); //there could still be leading/trailing whitespaces in the individual field name
		IOUtils::toUpper( tmp );
		IOUtils::removeDuplicateWhitespaces(tmp); //replace internal spaces by '_'
		IOUtils::replaceWhitespaces(tmp, '_');
		if (tmp.empty()) continue;
		
		if (tmp.compare("TIMESTAMP")==0 || tmp.compare("TS")==0 || tmp.compare("DATETIME")==0) {
			if (dt_as_decimal) 
				date_cols.decimal_date = ii;
			else 
				date_cols.date_str = date_cols.time_str = ii;
			skip_fields[ ii ] = true; //all special fields are marked as SKIP since they are read in a special way
		} else if (tmp.compare("DATE")==0 || tmp.compare("GIORNO")==0 || tmp.compare("FECHA")==0) {
			date_cols.date_str = ii;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("TIME")==0 || tmp.compare("ORA")==0 || tmp.compare("HORA")==0) {
			date_cols.time_str = ii;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("SKIP")==0) {
			skip_fields[ ii ] = true;
		} else if (tmp.compare("YEAR")==0) {
			date_cols.year = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("JDAY")==0 || tmp.compare("JDN")==0 || tmp.compare("YDAY")==0 || tmp.compare("DAY_OF_YEAR")==0 || tmp.compare("DOY")==0) {
			date_cols.jdn = ii;
			skip_fields[ ii ] = true;
			dt_as_year_and_jdn = true;
		} else if (tmp.compare("MONTH")==0) {
			date_cols.month = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("DAY")==0) {
			date_cols.day = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("NTIME")==0) {
			date_cols.time = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("HOUR")==0 || tmp.compare("HOURS")==0) {
			date_cols.hours = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("MINUTE")==0 || tmp.compare("MINUTES")==0) {
			date_cols.minutes = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("SECOND")==0 || tmp.compare("SECONDS")==0) {
			date_cols.seconds = ii;
			dt_as_components = true;
			skip_fields[ ii ] = true;
		} else if (tmp.compare("ID")==0 || tmp.compare("STATIONID")==0) {
			ID_col = ii;
			skip_fields[ ii ] = true;
		}
		
		//tmp = identifyField( tmp ); //try to identify known fields
	}
	date_cols.updateMaxCol();
	
	//check for time handling consistency
	if (!date_cols.isSet()) throw UnknownValueException("Please define how to parse the date and time information (as strings, decimal or components). Identified fields: "+date_cols.toString(), AT);
	if (dt_as_components && !single_field.empty())
		throw InvalidArgumentException("It is not possible to provide date/time as individual components and declare CSV_SINGLE_PARAM_INDEX", AT);

	//if necessary, set the format to the appropriate defaults
	if (!dt_as_decimal) {
		if (date_cols.date_str==date_cols.time_str) {
			if (datetime_idx.empty())
				setDateTimeSpec("YYYY-MM-DDTHH24:MI:SS");
		} else {
			if (date_cols.date_str!=IOUtils::npos && datetime_idx.empty())
				setDateTimeSpec("YYYY-MM-DD");
			if (date_cols.time_str!=IOUtils::npos && time_idx.empty())
				setTimeSpec("HH24:MI:SS");
		}
	}

	//the user wants to keep only one column, find the one he wants...
	//if there is a parameter name from the filename or header it has priority:
	if (!single_field.empty() && !user_provided_field_names) {
		if (ID_col!=IOUtils::npos) throw InvalidArgumentException("It is not possible set CSV_SINGLE_PARAM_INDEX when multiple stations are present within one single file with an ID field", AT);
		if (single_param_idx < fieldNames.size()) { //an index for the parameter column was given by the user
			fieldNames[single_param_idx] = single_field; //if this is wrongly date or time it has no effect on SMET output as long as we don't change date_cols.date_str
		} else if (date_cols.date_str == date_cols.time_str && fieldNames.size() == 2) { //no index given but unambiguous
			const size_t pidx = (date_cols.date_str == 0)? 1 : 0; //field that is not datetime
			fieldNames[pidx] = single_field;
		} else if (date_cols.date_str != date_cols.time_str && fieldNames.size() == 3) {
			size_t pidx;
			for (pidx = 0; pidx < 3; ++pidx) //look for 3rd field that is neither date nor time
				if (pidx != date_cols.date_str && pidx != date_cols.time_str) break;
			fieldNames[pidx] = single_field;
		}
	}
}

//very basic units parsing: a few hard-coded units are recognized and provide the necessary
//offset and multiplier to convert the values back to SI
void CsvParameters::setUnits(const std::string& csv_units, const char& delim)
{
	static const size_t nrStdUnits = 12; //NOTE: do not forget to update this index when editing stdUnits!!
	static const std::string stdUnits[nrStdUnits] = {"TS", "RN", "W/M2", "M/S", "K", "M", "N", "V", "VOLT", "DEG", "°", "KG/M2"};
	static const std::set<std::string> noConvUnits( stdUnits, stdUnits+nrStdUnits );

	std::vector<std::string> units;
	IOUtils::readLineToVec(csv_units, units, delim);
	units_offset.resize(units.size(), 0.);
	units_multiplier.resize(units.size(), 1.);
	
	for (size_t ii=0; ii<units.size(); ii++) {
		std::string tmp( units[ii] );
		IOUtils::toUpper( tmp );
		IOUtils::removeQuotes(tmp);
		if (tmp.empty() || tmp=="1" || tmp=="-" || tmp=="0 OR 1" || tmp=="0/1" || tmp=="??") continue; //empty unit
		if (noConvUnits.count(tmp)>0) continue; //this unit does not need conversion
		
		if (tmp=="%" || tmp=="pc" || tmp=="CM") units_multiplier[ii] = 0.01;
		else if (tmp=="C" || tmp=="DEGC" || tmp=="GRAD C" || tmp=="°C") units_offset[ii] = Cst::t_water_freezing_pt;
		else if (tmp=="HPA") units_multiplier[ii] = 1e2;
		else if (tmp=="MM" || tmp=="MV" || tmp=="MA") units_multiplier[ii] = 1e-3;
		else if (tmp=="MIN") units_multiplier[ii] = 60.;
		else if (tmp=="IN") units_multiplier[ii] = 0.0254;
		else if (tmp=="FT") units_multiplier[ii] = 0.3048;
		else if (tmp=="F") { units_multiplier[ii] = 5./9.; units_offset[ii] = -32.*5./9.;}
		else if (tmp=="KM/H") units_multiplier[ii] = 1./3.6;
		else if (tmp=="MPH") units_multiplier[ii] = 1.60934 / 3.6;
		else if (tmp=="KT") units_multiplier[ii] = 1.852 / 3.6;
		else {
			std::cerr << "CsvIO: Can not parse unit '" << tmp << "', please inform the MeteoIO developers\n";
		}
	}
}

//read and parse the file's headers in order to extract all possible information (including how to interpret the date/time information)
void CsvParameters::setFile(const std::string& i_file_and_path, const std::vector<std::string>& vecMetaSpec, const std::string& filename_spec, const std::string& station_idx)
{
	file_and_path = i_file_and_path;
	const std::multimap< size_t, std::pair<size_t, std::string> > meta_spec( parseHeadersSpecs(vecMetaSpec) );
	double lat = IOUtils::nodata;
	double lon = IOUtils::nodata;
	double easting = IOUtils::nodata, northing = IOUtils::nodata;
	
	//read and parse the file's headers
	if (!FileUtils::fileExists(file_and_path)) throw AccessException("File "+file_and_path+" does not exists", AT); //prevent invalid filenames
	errno = 0;
	if (!filename_spec.empty()) parseFileName( file_and_path, filename_spec, lat, lon, easting, northing);
	std::ifstream fin(file_and_path.c_str(), ios::in|ios::binary); //ascii does end of line translation, which messes up the pointer code
	if (fin.fail()) {
		const std::string msg( "Error opening file " + file_and_path + " for reading, possible reason: " + std::strerror(errno) + " Please check file existence and permissions!" );
		throw AccessException(msg, AT);
	}
	
	const bool user_auto_wrap = date_cols.auto_wrap; //we might trigger it, so we must reset it after the pre-reading
	const bool read_units = (units_headers!=IOUtils::npos && units_offset.empty() && units_multiplier.empty());
	size_t linenr=0;
	std::string line;
	std::vector<std::string> headerFields; //this contains the column headers from the file itself
	std::vector<std::string> tmp_vec; //to read a few lines of data
	Date prev_dt;
	size_t count_asc=0, count_dsc=0; //count how many ascending/descending timestamps are present
	static const size_t min_valid_lines = 10; //we want to correctly parse at least that many lines before quitting our sneak peek into the file
	const bool delimIsNoWS = (csv_delim!=' ');
	const bool hasHeaderRepeatMk = (!header_repeat_mk.empty());
	bool fields_ready = false;
	try {
		eoln = FileUtils::getEoln(fin);
		for (size_t ii=0; ii<(header_lines+1000); ii++) {
			getline(fin, line, eoln); //read complete line
			IOUtils::trim(line);
			if (fin.eof()) {
				if (header_repeat_at_start) linenr++; //since it was not incremented when matching the repeat header marker
				if (linenr>header_lines) break; //eof while reading the data section
				std::ostringstream ss;
				ss << "Declaring " << header_lines << " header line(s) for file " << file_and_path << ", but it only contains " << linenr << " lines";
				throw InvalidArgumentException(ss.str(), AT);
			}
			if (hasHeaderRepeatMk && !header_repeat_at_start && line.find(header_repeat_mk)!=std::string::npos) {
				header_repeat_at_start = true; //so we won't match another header_repeat_mk marker
				continue; //the line count it not incremented so the special headers still keep logical indices
			}
			linenr++;
			if (comments_mk!='\n') IOUtils::stripComments(line, comments_mk);
			if (line.empty()) continue;
			if (*line.rbegin()=='\r') line.erase(line.end()-1); //getline() skipped \n, so \r comes in last position
			
			if (meta_spec.count(linenr)>0) 
				parseSpecialHeaders(line, linenr, meta_spec, lat, lon, easting, northing);
			if (linenr==columns_headers) { //so user provided csv_fields have priority. If columns_headers==npos, this will also never be true
				if (delimIsNoWS) { //even if header_delim is set, we expect the fields to be separated by csv_delim
					IOUtils::cleanFieldName(line, false); //we'll handle whitespaces when parsing
					IOUtils::readLineToVec(line, headerFields, csv_delim);
				} else {
					IOUtils::cleanFieldName(line, false); //don't touch whitespaces
					IOUtils::readLineToVec(line, headerFields);
				}
			}
			if (read_units && linenr==units_headers)
				setUnits(line, csv_delim);

			if (linenr<=header_lines) continue; //we are still parsing the header
			if (!fields_ready) { //we should now have all the information from the headers, so build what we need for data parsing
				parseFields(headerFields, csv_fields);
				fields_ready=true;
				continue;
			}
			
			const size_t nr_curr_data_fields = (delimIsNoWS)? IOUtils::readLineToVec(line, tmp_vec, csv_delim) : IOUtils::readLineToVec(line, tmp_vec);
			if (nr_curr_data_fields>date_cols.max_dt_col) {
				const Date dt( parseDate(tmp_vec) );
				if (dt.isUndef()) continue;
				if (!prev_dt.isUndef()) {
					if (dt>prev_dt) count_asc++;
					else count_dsc++;
				}
				prev_dt = dt;
			}
			if (count_asc+count_dsc >= min_valid_lines) break; //we've add enough valid lines to understand the file, quitting
		}
	} catch (...) {
		fin.close();
		throw;
	}
	fin.close();
	date_cols.auto_wrap = user_auto_wrap; //resetting it since we might have triggered it
	if (!date_cols.isSet()) 
		throw NoDataException("Date and time parsing not properly initialized, please contact the MeteoIO developers!", AT);

	if (count_dsc>count_asc) asc_order=false;
	
	if (lat!=IOUtils::nodata || lon!=IOUtils::nodata) {
		const double alt = location.getAltitude(); //so we don't change previously set altitude
		location.setLatLon(lat, lon, alt, false); //we let Coords handle possible missing data / wrong values, etc
	}
	if (easting!=IOUtils::nodata || northing!=IOUtils::nodata) {
		const double alt = location.getAltitude();
		location.setXY(easting, northing, alt, false); //coord system was set on keyword parsing
	}
	//location is either coming from POSITIONxx ini keys or from file name parsing or from header parsing
	if (location.isNodata()) 
		throw NoDataException("Missing geographic coordinates for '"+i_file_and_path+"', please consider providing the POSITION ini key", AT);
	location.check("Inconsistent geographic coordinates in file \"" + file_and_path + "\": ");
	
	if (name.empty()) name = FileUtils::removeExtension( FileUtils::getFilename(i_file_and_path) ); //fallback if nothing else could be find
	if (id.empty()) {
		if (station_idx.empty()) 
			id = name; //really nothing, copy "name"
		else
			id = "ID"+station_idx; //automatic numbering of default IDs
	}
}

//check that the format is usable (and prevent parameters injection / buffer overflows)
void CsvParameters::checkSpecString(const std::string& spec_string, const size_t& nr_params)
{
	const size_t nr_percent = (unsigned)std::count(spec_string.begin(), spec_string.end(), '%');
	const size_t nr_placeholders0 = IOUtils::count(spec_string, "%f");
	const size_t nr_placeholders2 = IOUtils::count(spec_string, "%2f");
	const size_t nr_placeholders4 = IOUtils::count(spec_string, "%4f");
	const size_t nr_placeholders5 = IOUtils::count(spec_string, "%32s");
	size_t nr_placeholders = (nr_placeholders0!=std::string::npos)? nr_placeholders0 : 0;
	nr_placeholders += (nr_placeholders2!=std::string::npos)? nr_placeholders2 : 0;
	nr_placeholders += (nr_placeholders4!=std::string::npos)? nr_placeholders4 : 0;
	nr_placeholders += (nr_placeholders5!=std::string::npos)? nr_placeholders5 : 0;
	const size_t pos_pc_pc = spec_string.find("%%");
	if (nr_percent!=nr_params || nr_percent!=nr_placeholders || pos_pc_pc!=std::string::npos)
		throw InvalidFormatException("Badly formatted date/time specification '"+spec_string+"': argument appearing twice or using '%%'", AT);
}

//from a SPEC string such as "DD.MM.YYYY HH24:MIN:SS", build the format string for scanf as well as the parameters indices
//the indices are based on ISO timestamp, so year=0, month=1, ..., ss=5 while tz is handled separately
void CsvParameters::setDateTimeSpec(const std::string& datetime_spec)
{
	static const char* keys[6] = {"YYYY", "MM", "DD", "HH24", "MI", "SS"};
	std::vector< std::pair<size_t, size_t> > sorting_vector;
	for (size_t ii=0; ii<6; ii++) {
		const size_t key_pos = datetime_spec.find( keys[ii] );
		if (key_pos!=std::string::npos)
			sorting_vector.push_back( make_pair( key_pos, ii) );
	}
	
	//fill datetime_idx as a vector of [0-5] indices (for ISO fields) in the order they appear in the user-provided format string
	std::sort(sorting_vector.begin(), sorting_vector.end(), &sort_dateKeys);
	for (size_t ii=0; ii<sorting_vector.size(); ii++)
		datetime_idx.push_back( sorting_vector[ii].second );
	
	datetime_format = datetime_spec;
	const size_t tz_pos = datetime_format.find("TZ");
	if (tz_pos!=std::string::npos) {
		if (tz_pos!=(datetime_format.length()-2))
			throw InvalidFormatException("When providing TZ in a date/time format, it must be at the very end of the string", AT);
		has_tz = true;
		datetime_format.replace(tz_pos, 2, "%32s");
	}
	IOUtils::replace_all(datetime_format, "DD", "%2f");
	IOUtils::replace_all(datetime_format, "MM", "%2f");
	IOUtils::replace_all(datetime_format, "YYYY", "%4f");
	IOUtils::replace_all(datetime_format, "HH24", "%2f");
	IOUtils::replace_all(datetime_format, "MI", "%2f");
	IOUtils::replace_all(datetime_format, "SS", "%f");
	
	const size_t nr_params_check = (has_tz)? datetime_idx.size()+1 : datetime_idx.size();
	checkSpecString(datetime_format, nr_params_check);
}

void CsvParameters::setTimeSpec(const std::string& time_spec)
{
	if (time_spec.empty()) return;
	static const char* keys[3] = {"HH24", "MI", "SS"};
	std::vector< std::pair<size_t, size_t> > sorting_vector;
	for (size_t ii=0; ii<3; ii++) {
		const size_t key_pos = time_spec.find( keys[ii] );
		if (key_pos!=std::string::npos)
			sorting_vector.push_back( make_pair( key_pos, ii) );
	}

	//fill time_idx as a vector of [0-3] indices (for ISO fields) in the order they appear in the user-provided format string
	std::sort(sorting_vector.begin(), sorting_vector.end(), &sort_dateKeys);
	for (size_t ii=0; ii<sorting_vector.size(); ii++)
		time_idx.push_back( sorting_vector[ii].second );

	time_format = time_spec;
	const size_t tz_pos = time_format.find("TZ");
	if (tz_pos!=std::string::npos) {
		if (tz_pos!=(time_format.length()-2))
			throw InvalidFormatException("When providing TZ in a date/time format, it must be at the very end of the string", AT);
		has_tz = true;
		time_format.replace(tz_pos, 2, "%32s");
	}
	IOUtils::replace_all(time_format, "HH24", "%2f");
	IOUtils::replace_all(time_format, "MI", "%2f");
	IOUtils::replace_all(time_format, "SS", "%f");

	const size_t nr_params_check = (has_tz)? time_idx.size()+1 : time_idx.size();
	checkSpecString(time_format, nr_params_check);
}

void CsvParameters::setDecimalDateType(std::string decimaldate_type)
{
	IOUtils::toUpper( decimaldate_type );
	if (decimaldate_type=="EXCEL") {
		date_cols.decimal_date_type = CsvDateTime::EXCEL;
	} else if (decimaldate_type=="JULIAN") {
		date_cols.decimal_date_type = CsvDateTime::JULIAN;
	} else if (decimaldate_type=="MJULIAN") {
		date_cols.decimal_date_type = CsvDateTime::MJULIAN;
	} else if (decimaldate_type=="MATLAB") {
		date_cols.decimal_date_type = CsvDateTime::MATLAB;
	} else if (decimaldate_type=="RFC868") {
		date_cols.decimal_date_type = CsvDateTime::RFC868;
	} else if (decimaldate_type=="UNIX") {
		date_cols.decimal_date_type = CsvDateTime::UNIX;
	} else
		throw InvalidArgumentException("Unknown decimal date type '"+decimaldate_type+"'", AT);
	
	dt_as_decimal = true;
}

void CsvParameters::setFixedYear(const int& i_year, const bool& auto_wrap)
{
	date_cols.year_cst = i_year;
	date_cols.auto_wrap = auto_wrap;
}

//check that all arguments are integers except the seconds, then build a Date
Date CsvParameters::createDate(const float args[6], const double i_tz)
{
	int i_args[5] = {0, 0, 0, 0, 0};
	for (unsigned int ii=0; ii<5; ii++) {
		i_args[ii] = (int)args[ii];
		if ((float)i_args[ii]!=args[ii]) return Date();
	}
	
	return Date(i_args[0], i_args[1], i_args[2], i_args[3], i_args[4], static_cast<double>(args[5]), i_tz);
}

Date CsvParameters::parseDate(const std::string& date_str, const std::string& time_str) const
{
	float args[6] = {0., 0., 0., 0., 0., 0.};
	char rest[32] = "";
	bool status = false;
	switch( datetime_idx.size() ) {
		case 6:
			status = (sscanf(date_str.c_str(), datetime_format.c_str(), &args[ datetime_idx[0] ], &args[ datetime_idx[1] ], &args[ datetime_idx[2] ], &args[ datetime_idx[3] ], &args[ datetime_idx[4] ], &args[ datetime_idx[5] ], rest)>=6);
			break;
		case 5:
			status = (sscanf(date_str.c_str(), datetime_format.c_str(), &args[ datetime_idx[0] ], &args[ datetime_idx[1] ], &args[ datetime_idx[2] ], &args[ datetime_idx[3] ], &args[ datetime_idx[4] ], rest)>=5);
			break;
		case 4:
			status = (sscanf(date_str.c_str(), datetime_format.c_str(), &args[ datetime_idx[0] ], &args[ datetime_idx[1] ], &args[ datetime_idx[2] ], &args[ datetime_idx[3] ], rest)>=4);
			break;
		case 3:
			status = (sscanf(date_str.c_str(), datetime_format.c_str(), &args[ datetime_idx[0] ], &args[ datetime_idx[1] ], &args[ datetime_idx[2] ], rest)>=3);
			break;
		default: // do nothing;
			break;
	}
	if (!status) return Date(); //we MUST have read successfuly at least the date part

	if (!time_idx.empty()) {
		//there is a +3 offset because the first 3 positions are used by the date part
		switch( time_idx.size() ) {
			case 3:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0]+3 ], &args[ time_idx[1]+3 ], &args[ time_idx[2]+3 ], rest)>=3);
				break;
			case 2:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0]+3 ], &args[ time_idx[1]+3 ], rest)>=2);
				break;
			case 1:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0]+3 ], rest)>=1);
				break;
			default: // do nothing;
				break;
		}
	}

	if (!status) return Date();
	const double tz = (has_tz)? Date::parseTimeZone(rest) : csv_tz;
	return createDate(args, tz);
}

Date CsvParameters::parseJdnDate(const std::vector<std::string>& vecFields)
{
	//year + integer jdn + time string
	if (!time_idx.empty()) {
		int year=0;
		double jdn=0.;
		if (!parseDateComponent(vecFields, date_cols.jdn, jdn)) return Date();
		if (!parseDateComponent(vecFields, date_cols.year, year)) return Date();
		if (year==0 && date_cols.year_cst!=IOUtils::inodata) year = date_cols.getFixedYear( jdn ); //here jdn must be double
		
		//parse the timer information and compute the decimal jdn
		const std::string time_str( vecFields[ date_cols.time_str ] );
		float args[3] = {0., 0., 0.};
		char rest[32] = "";
		bool status = false;
		switch( time_idx.size() ) {
			case 3:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0] ], &args[ time_idx[1] ], &args[ time_idx[2] ], rest)>=3);
				break;
			case 2:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0] ], &args[ time_idx[1] ], rest)>=2);
				break;
			case 1:
				status = (sscanf(time_str.c_str(), time_format.c_str(), &args[ time_idx[0] ], rest)>=1);
				break;
			default: // do nothing;
				break;
		}
		if (!status) return Date();
		
		jdn += (static_cast<double>(args[0])*3600. + static_cast<double>(args[1])*60. + static_cast<double>(args[2])) / (24.*3600.);
		const double tz = (has_tz)? Date::parseTimeZone(rest) : csv_tz;
		return Date(year, jdn, tz);
	}
	
	//year + integer jdn + time numerical time, for example "952" for 09:52
	if (date_cols.time!=IOUtils::npos) {
		int year=0, jdn=0, time=0;
		if (!parseDateComponent(vecFields, date_cols.jdn, jdn)) return Date();
		if (!parseDateComponent(vecFields, date_cols.year, year)) return Date();
		if (year==0 && date_cols.year_cst!=IOUtils::inodata) year = date_cols.getFixedYear( static_cast<double>(jdn) );
		if (!parseDateComponent(vecFields, date_cols.time, time)) return Date();
		const int hours = time / 100;
		const int minutes = time - hours*100;
		
		return Date(year, static_cast<double>(jdn)+(hours*60.+minutes)/(24.*60.), csv_tz);
	}
	
	//year + integer jdn + hours + minutes, etc
	if (date_cols.hours!=IOUtils::npos) {
		int year=0, jdn=0, hours=0, minutes=0;
		double seconds=0;
		if (!parseDateComponent(vecFields, date_cols.jdn, jdn)) return Date();
		if (!parseDateComponent(vecFields, date_cols.year, year)) return Date();
		if (year==0 && date_cols.year_cst!=IOUtils::inodata) year = date_cols.getFixedYear( static_cast<double>(jdn) );
		if (!parseDateComponent(vecFields, date_cols.hours, hours)) return Date();
		if (!parseDateComponent(vecFields, date_cols.minutes, minutes)) return Date();
		if (!parseDateComponent(vecFields, date_cols.seconds, seconds)) return Date();
		
		return Date(year, static_cast<double>(jdn)+(hours*3600. + minutes*60. + seconds) / (24.*3600.), csv_tz);
	}
	
	//year + decimal jdn
	{ //ensure own scope to avoid variable names conflicts
		int year=0;
		double jdn = 0.;
		if (!parseDateComponent(vecFields, date_cols.jdn, jdn)) return Date();
		if (!parseDateComponent(vecFields, date_cols.year, year)) return Date();
		if (year==0 && date_cols.year_cst!=IOUtils::inodata) year = date_cols.getFixedYear( jdn );
		
		return Date(year, jdn, csv_tz);
	}
}

Date CsvParameters::parseDate(const std::string& value_str, const CsvDateTime::decimal_date_formats& format) const
{
	Date dt;
	
	if (format==CsvDateTime::UNIX) {
		time_t value;
		if (!IOUtils::convertString(value, value_str)) return dt;
		dt.setUnixDate(value);
		return dt;
	} else {
		double value=0.;
		if (!IOUtils::convertString(value, value_str)) return dt;
		
		if (format==CsvDateTime::EXCEL) {
			dt.setExcelDate(value, csv_tz);
		} else if (format==CsvDateTime::JULIAN) {
			dt.setDate(value, csv_tz);
		} else if (format==CsvDateTime::MJULIAN) {
			dt.setModifiedJulianDate(value, csv_tz);
		} else if (format==CsvDateTime::MATLAB) {
			dt.setMatlabDate(value, csv_tz);
		} else if (format==CsvDateTime::RFC868) {
			dt.setRFC868Date(value, csv_tz);
		} 
	}
	
	return dt;
}

bool CsvParameters::parseDateComponent(const std::vector<std::string>& vecFields, const size_t& idx, int& value)
{
	if (idx==IOUtils::npos) {
		value=0;
		return true;
	}
	
	return IOUtils::convertString(value, vecFields[ idx ]);
}

bool CsvParameters::parseDateComponent(const std::vector<std::string>& vecFields, const size_t& idx, double& value)
{
	if (idx==IOUtils::npos) {
		value=0.;
		return true;
	}
	
	return IOUtils::convertString(value, vecFields[ idx ]);
}

Date CsvParameters::parseDate(const std::vector<std::string>& vecFields)
{
	//TODO: one of the strings + components
	if (dt_as_components) { //date and time components split as columns.
		//As year + jdn + time as string or components
		if (dt_as_year_and_jdn) return parseJdnDate(vecFields);
		
		//As pure components: year, month, day, 
		int year=0, month=0, day=0, hour=0, minute=0;
		double seconds = 0.;
		
		if (!parseDateComponent(vecFields, date_cols.month, month)) return Date();
		if (!parseDateComponent(vecFields, date_cols.year, year)) return Date();
		if (year==0 && date_cols.year_cst!=IOUtils::inodata) year = date_cols.getFixedYear( month );
		if (!parseDateComponent(vecFields, date_cols.day, day)) return Date();
		if (!parseDateComponent(vecFields, date_cols.hours, hour)) return Date();
		if (!parseDateComponent(vecFields, date_cols.minutes, minute)) return Date();
		if (!parseDateComponent(vecFields, date_cols.seconds, seconds)) return Date();
		
		return Date(year, month, day, hour, minute, seconds, csv_tz);
	} else if (dt_as_decimal) {
		return parseDate(vecFields[ date_cols.decimal_date ], date_cols.decimal_date_type);
	} else {
		return parseDate(vecFields[ date_cols.date_str ], vecFields[ date_cols.time_str ]);
	}
}

StationData CsvParameters::getStation() const 
{
	StationData sd(location, id, name);
	if (slope==0. || (slope!=IOUtils::nodata && azi!=IOUtils::nodata))
		sd.setSlope(slope, azi);
	return sd;
}


///////////////////////////////////////////////////// Now the real CsvIO class starts //////////////////////////////////////////
const size_t CsvIO::streampos_every_n_lines = 2000; //save streampos every 2000 lines of data

CsvIO::CsvIO(const std::string& configfile) 
      : cfg(configfile), indexer_map(), csvparam(), vecStations(),
        coordin(), coordinparam(), silent_errors(false), errors_to_nodata(false) { parseInputOutputSection(); }

CsvIO::CsvIO(const Config& cfgreader)
      : cfg(cfgreader), indexer_map(), csvparam(), vecStations(),
        coordin(), coordinparam(), silent_errors(false), errors_to_nodata(false) { parseInputOutputSection(); }

void CsvIO::parseInputOutputSection()
{
	IOUtils::getProjectionParameters(cfg, coordin, coordinparam);
	
	cfg.getValue("CSV_SILENT_ERRORS", "Input", silent_errors, IOUtils::nothrow);
	cfg.getValue("CSV_ERRORS_TO_NODATA", "Input", errors_to_nodata, IOUtils::nothrow);

	const double in_TZ = cfg.get("TIME_ZONE", "Input");
	const std::string meteopath = cfg.get("METEOPATH", "Input");
	std::vector< std::pair<std::string, std::string> > vecFilenames( cfg.getValues("STATION", "INPUT") );

	if (vecFilenames.empty()) { //scan all of the data path for a given file extension if no stations are specified
		bool is_recursive = false;
		std::string csvext(".csv");
		cfg.getValue("METEOPATH_RECURSIVE", "Input", is_recursive, IOUtils::nothrow);
		cfg.getValue("CSV_FILE_EXTENSION", "Input", csvext, IOUtils::nothrow); 
		
		std::list<std::string> dirlist( FileUtils::readDirectory(meteopath, csvext, is_recursive) );
		dirlist.sort();

		size_t hit = 0; //human readable iterator
		for (std::list<std::string>::iterator it=dirlist.begin(); it!=dirlist.end(); ++it) {
			hit++;
			std::stringstream ss;
			ss << "STATION" << hit; //assign alphabetically ordered ID to station
			
			const std::pair<std::string, std::string> stat_id_and_name(ss.str(), *it);
			vecFilenames.push_back(stat_id_and_name);
		}
	} 
	
	for (size_t ii=0; ii<vecFilenames.size(); ii++) {
		const std::string idx( vecFilenames[ii].first.substr(string("STATION").length()) );
		static const std::string dflt("CSV_"); //the prefix for a key for ALL stations
		const std::string pre( "CSV"+idx+"_" ); //the prefix for the current station only
		
		CsvParameters tmp_csv(in_TZ);
		std::string coords_specs;
		if (cfg.keyExists("POSITION"+idx, "INPUT")) cfg.getValue("POSITION"+idx, "INPUT", coords_specs);
		else cfg.getValue("POSITION", "INPUT", coords_specs, IOUtils::nothrow);
		
		std::string name;
		if (cfg.keyExists(pre+"NAME", "Input")) cfg.getValue(pre+"NAME", "Input", name);
		else cfg.getValue(dflt+"NAME", "Input", name, IOUtils::nothrow);
		
		std::string id;
		if (cfg.keyExists(pre+"ID", "Input")) cfg.getValue(pre+"ID", "Input", id);
		else cfg.getValue(dflt+"ID", "Input", id, IOUtils::nothrow);
		if (!coords_specs.empty()) {
			const Coords loc(coordin, coordinparam, coords_specs);
			tmp_csv.setLocation(loc, name, id);
		} else {
			tmp_csv.setLocation(Coords(), name, id);
		}
		
		double slope=IOUtils::nodata;
		if (cfg.keyExists(pre+"SLOPE", "INPUT")) cfg.getValue(pre+"SLOPE", "INPUT", slope);
		else cfg.getValue(dflt+"SLOPE", "INPUT", slope, IOUtils::nothrow);
		double azimuth=IOUtils::nodata;
		if (cfg.keyExists(pre+"AZIMUTH", "INPUT")) cfg.getValue(pre+"AZIMUTH", "INPUT", azimuth);
		else cfg.getValue(dflt+"AZIMUTH", "INPUT", azimuth, IOUtils::nothrow);
		tmp_csv.setSlope(slope, azimuth);
		
		if (cfg.keyExists(pre+"NODATA", "Input")) cfg.getValue(pre+"NODATA", "Input", tmp_csv.nodata);
		else cfg.getValue(dflt+"NODATA", "Input", tmp_csv.nodata, IOUtils::nothrow);
		
		std::string delim_spec(","); //default delimiter
		if (cfg.keyExists(pre+"DELIMITER", "Input")) cfg.getValue(pre+"DELIMITER", "Input", delim_spec);
		else cfg.getValue(dflt+"DELIMITER", "Input", delim_spec, IOUtils::nothrow);
		tmp_csv.setDelimiter(delim_spec);
		
		bool purgeQuotes=false;
		if (cfg.keyExists(pre+"DEQUOTE", "Input")) cfg.getValue(pre+"DEQUOTE", "Input", purgeQuotes);
		else cfg.getValue(dflt+"DEQUOTE", "Input", purgeQuotes, IOUtils::nothrow);
		tmp_csv.setPurgeQuotes(purgeQuotes);
		
		char comments_mk='\n';
		if (cfg.keyExists(pre+"COMMENTS_MK", "Input")) cfg.getValue(pre+"COMMENTS_MK", "Input", comments_mk);
		else cfg.getValue(dflt+"COMMENTS_MK", "Input", comments_mk, IOUtils::nothrow);
		if (comments_mk!='\n') tmp_csv.comments_mk = comments_mk;
		
		size_t single_parameter_index;
		if (cfg.keyExists(pre+"SINGLE_PARAM_INDEX", "Input")) {
			cfg.getValue(pre+"SINGLE_PARAM_INDEX", "Input", single_parameter_index);
			tmp_csv.single_param_idx = single_parameter_index - 1; //counting starts at 1 in ini file
		} else if (cfg.keyExists(dflt+"SINGLE_PARAM_INDEX", "Input")) {
			cfg.getValue(dflt+"SINGLE_PARAM_INDEX", "Input", single_parameter_index);
			tmp_csv.single_param_idx = single_parameter_index - 1;
		}

		std::string header_delim_spec;
		if (cfg.keyExists(pre+"HEADER_DELIMITER", "Input"))
			cfg.getValue(pre+"HEADER_DELIMITER", "Input", header_delim_spec);
		else if (cfg.keyExists(dflt+"HEADER_DELIMITER", "Input"))
			cfg.getValue(dflt+"HEADER_DELIMITER", "Input", header_delim_spec);
		else
			header_delim_spec = delim_spec;
		tmp_csv.setHeaderDelimiter(header_delim_spec);

		std::string hdr_repeat_mk;
		if (cfg.keyExists(pre+"HEADER_REPEAT_MK", "Input")) cfg.getValue(pre+"HEADER_REPEAT_MK", "Input", hdr_repeat_mk);
		else cfg.getValue(dflt+"HEADER_REPEAT_MK", "Input", hdr_repeat_mk, IOUtils::nothrow);
		tmp_csv.setHeaderRepeatMk(hdr_repeat_mk);
		
		if (cfg.keyExists(pre+"NR_HEADERS", "Input")) cfg.getValue(pre+"NR_HEADERS", "Input", tmp_csv.header_lines);
		else cfg.getValue(dflt+"NR_HEADERS", "Input", tmp_csv.header_lines, IOUtils::nothrow);
		
		if (cfg.keyExists(pre+"COLUMNS_HEADERS", "Input")) cfg.getValue(pre+"COLUMNS_HEADERS", "Input", tmp_csv.columns_headers);
		else cfg.getValue(dflt+"COLUMNS_HEADERS", "Input", tmp_csv.columns_headers, IOUtils::nothrow);
		if (tmp_csv.columns_headers>tmp_csv.header_lines) tmp_csv.columns_headers = IOUtils::npos;
		
		if (cfg.keyExists(pre+"FIELDS", "Input")) cfg.getValue(pre+"FIELDS", "Input", tmp_csv.csv_fields);
		else cfg.getValue(dflt+"FIELDS", "Input", tmp_csv.csv_fields, IOUtils::nothrow);
		
		if (tmp_csv.columns_headers==IOUtils::npos && tmp_csv.csv_fields.empty())
			throw InvalidArgumentException("Please provide either CSV_COLUMNS_HEADERS (make sure it is <= CSV_NR_HEADERS) or CSV_FIELDS", AT);
		
		if (cfg.keyExists(pre+"FILTER_ID", "Input")) cfg.getValue(pre+"FILTER_ID", "Input", tmp_csv.filter_ID);
		else cfg.getValue(dflt+"FILTER_ID", "Input", tmp_csv.filter_ID, IOUtils::nothrow);
		
		std::vector<size_t> vecSkipFields;
		if (cfg.keyExists(pre+"SKIP_FIELDS", "Input")) cfg.getValue(pre+"SKIP_FIELDS", "Input", vecSkipFields);
		else cfg.getValue(dflt+"SKIP_FIELDS", "Input", vecSkipFields, IOUtils::nothrow);
		tmp_csv.setSkipFields( vecSkipFields );
		
		if (cfg.keyExists(pre+"UNITS_HEADERS", "Input")) cfg.getValue(pre+"UNITS_HEADERS", "Input", tmp_csv.units_headers);
		else cfg.getValue(dflt+"UNITS_HEADERS", "Input", tmp_csv.units_headers, IOUtils::nothrow);
		
		if (cfg.keyExists(pre+"UNITS_OFFSET", "Input")) cfg.getValue(pre+"UNITS_OFFSET", "Input", tmp_csv.units_offset);
		else cfg.getValue(dflt+"UNITS_OFFSET", "Input", tmp_csv.units_offset, IOUtils::nothrow);
		
		if (cfg.keyExists(pre+"UNITS_MULTIPLIER", "Input")) cfg.getValue(pre+"UNITS_MULTIPLIER", "Input", tmp_csv.units_multiplier);
		else cfg.getValue(dflt+"UNITS_MULTIPLIER", "Input", tmp_csv.units_multiplier, IOUtils::nothrow);
		
		std::string csv_units;
		if (cfg.keyExists(pre+"UNITS", "Input")) cfg.getValue(pre+"UNITS", "Input", csv_units);
		else cfg.getValue(dflt+"UNITS", "Input", csv_units, IOUtils::nothrow);
		if (!csv_units.empty()) {
			if (!tmp_csv.units_multiplier.empty() || !tmp_csv.units_offset.empty())
				throw InvalidArgumentException("It is not possible to define both CSV_UNITS and CSV_UNITS_OFFSET or CSV_UNITS_MULTIPLIER", AT);
			tmp_csv.setUnits( csv_units, ' ' );
		}
		
		//Date and time formats. The defaults will be set when parsing the column names (so they are appropriate for the available columns)
		std::string datetime_spec, date_spec, time_spec, decimaldate_type;
		if (cfg.keyExists(pre+"DECIMALDATE_TYPE", "Input")) cfg.getValue(pre+"DECIMALDATE_TYPE", "Input", decimaldate_type);
		if (cfg.keyExists(pre+"DATETIME_SPEC", "Input")) cfg.getValue(pre+"DATETIME_SPEC", "Input", datetime_spec);
		if (cfg.keyExists(pre+"DATE_SPEC", "Input")) cfg.getValue(pre+"DATE_SPEC", "Input", date_spec);
		if (cfg.keyExists(pre+"TIME_SPEC", "Input")) cfg.getValue(pre+"TIME_SPEC", "Input", time_spec);
		if (decimaldate_type.empty() && datetime_spec.empty() && date_spec.empty() && time_spec.empty() && datetime_spec.empty()) {
			cfg.getValue(dflt+"DECIMALDATE_TYPE", "Input", decimaldate_type, IOUtils::nothrow);
			cfg.getValue(dflt+"DATETIME_SPEC", "Input", datetime_spec, IOUtils::nothrow);
			cfg.getValue(dflt+"DATE_SPEC", "Input", date_spec, IOUtils::nothrow);
			cfg.getValue(dflt+"TIME_SPEC", "Input", time_spec, IOUtils::nothrow);
		}

		if (!decimaldate_type.empty() && (!datetime_spec.empty() || !date_spec.empty() || !time_spec.empty() ))
			throw InvalidArgumentException("It is not possible to define both decimaldate_type and other date / time specifications", AT);
		if (!datetime_spec.empty() && (!date_spec.empty() || !time_spec.empty()) )
			throw InvalidArgumentException("It is not possible to define both datetime_spec and date_spec or time_spec", AT);
		if ((!date_spec.empty() && time_spec.empty()) || (date_spec.empty() && !time_spec.empty()))
			throw InvalidArgumentException("Please define both date_spec and time_spec, ", AT);
		
		if (!decimaldate_type.empty()) {
			tmp_csv.setDecimalDateType(decimaldate_type);
		} else if (!datetime_spec.empty()) {
			tmp_csv.setDateTimeSpec(datetime_spec);
		} else {
			if (!date_spec.empty())
				tmp_csv.setDateTimeSpec(date_spec);
			if (!time_spec.empty()) 
				tmp_csv.setTimeSpec(time_spec);
		}
		
		int fixed_year=IOUtils::inodata;
		if (cfg.keyExists(pre+"FALLBACK_YEAR", "Input")) cfg.getValue(pre+"FALLBACK_YEAR", "Input", fixed_year);
		else cfg.getValue(dflt+"FALLBACK_YEAR", "Input", fixed_year, IOUtils::nothrow);
		bool auto_wrap_year=true;
		if (cfg.keyExists(pre+"FALLBACK_AUTO_WRAP", "Input")) cfg.getValue(pre+"FALLBACK_AUTO_WRAP", "Input", auto_wrap_year);
		else cfg.getValue(dflt+"FALLBACK_AUTO_WRAP", "Input", auto_wrap_year, IOUtils::nothrow);
		if (fixed_year!=IOUtils::inodata) tmp_csv.setFixedYear( fixed_year, auto_wrap_year );
		
		std::vector<std::string> vecMetaSpec;
		if (cfg.keyExists(pre+"SPECIAL_HEADERS", "Input")) cfg.getValue(pre+"SPECIAL_HEADERS", "Input", vecMetaSpec);
		else cfg.getValue(dflt+"SPECIAL_HEADERS", "Input", vecMetaSpec, IOUtils::nothrow);
		
		std::string filename_spec;
		if (cfg.keyExists(pre+"FILENAME_SPEC", "Input")) cfg.getValue(pre+"FILENAME_SPEC", "Input", filename_spec);
		else cfg.getValue(dflt+"FILENAME_SPEC", "Input", filename_spec, IOUtils::nothrow);
		
		tmp_csv.setFile(meteopath + "/" + vecFilenames[ii].second, vecMetaSpec, filename_spec, idx);
		csvparam.push_back( tmp_csv );
	}
}

void CsvIO::readStationData(const Date& /*date*/, std::vector<StationData>& vecStation)
{
	vecStation.clear();
	for (size_t ii=0; ii<csvparam.size(); ii++)
		vecStation.push_back( csvparam[ii].getStation() );
}

//build MeteoData template, based on parameters available in the csv file
MeteoData CsvIO::createTemplate(const CsvParameters& params)
{
	const size_t nr_of_data_fields = params.csv_fields.size(); //this has been checked by CsvParameters
	
	//build MeteoData template
	MeteoData template_md( Date(0., 0.), params.getStation() );
	for (size_t ii=0; ii<nr_of_data_fields; ii++) {
		if (params.skip_fields.count(ii)>0) continue;
		template_md.addParameter( params.csv_fields[ii] );
	}

	return template_md;
}

Date CsvIO::getDate(CsvParameters& params, const std::vector<std::string>& vecFields, const bool& silent_errors, const std::string& filename, const size_t& linenr)
{
	const Date dt( params.parseDate(vecFields) );
	if (dt.isUndef()) {
		const std::string err_msg( "Date or time could not be read in file \'"+filename+"' at line "+IOUtils::toString(linenr) );
		if (silent_errors)
			std::cerr << err_msg << "\n";
		else 
			throw InvalidFormatException(err_msg, AT);
	}
	return dt;
}

std::vector<MeteoData> CsvIO::readCSVFile(CsvParameters& params, const Date& dateStart, const Date& dateEnd)
{
	const std::string filename( params.getFilename() );
	size_t nr_of_data_fields = params.csv_fields.size(); //this has been checked by CsvParameters
	const bool use_offset = !params.units_offset.empty();
	const bool use_multiplier = !params.units_multiplier.empty();
	if ((use_offset && params.units_offset.size()!=nr_of_data_fields) || (use_multiplier && params.units_multiplier.size()!=nr_of_data_fields)) {
		const std::string msg( "in file '"+filename+"', the declared units_offset ("+IOUtils::toString(params.units_offset.size())+") / units_multiplier ("+IOUtils::toString(params.units_multiplier.size())+") must match the number of columns ("+IOUtils::toString(nr_of_data_fields)+") in the file!" );
		throw InvalidFormatException(msg, AT);
	}

	const MeteoData template_md( createTemplate(params) );

	//now open the file
	if (!FileUtils::fileExists(filename)) throw AccessException("File '"+filename+"' does not exists", AT); //prevent invalid filenames
	errno = 0;
	std::ifstream fin(filename.c_str(), ios::in|ios::binary); //ascii does end of line translation, which messes up the pointer code
	if (fin.fail()) {
		std::ostringstream ss;
		ss << "Error opening file \"" << filename << "\" for reading, possible reason: " << std::strerror(errno);
		ss << " Please check file existence and permissions!";
		throw AccessException(ss.str(), AT);
	}
	
	std::string line;
	size_t linenr=0;
	streampos fpointer = indexer_map[filename].getIndex(dateStart);
	if (fpointer!=static_cast<streampos>(-1) && params.asc_order) {
		fin.seekg(fpointer); //a previous pointer was found, jump to it
	} else {
		//skip the headers (they have been read already, so we know this works)
		const size_t skip_count = (params.header_repeat_at_start)? params.header_lines+1 : params.header_lines;
		FileUtils::skipLines(fin, skip_count);
		linenr += skip_count;
	}
	
	//and now, read the data and fill the vector vecMeteo
	std::vector<MeteoData> vecMeteo;
	std::vector<std::string> tmp_vec;
	const std::string nodata( params.nodata );
	const std::string nodata_with_quotes( "\""+params.nodata+"\"" );
	const std::string nodata_with_single_quotes( "\'"+params.nodata+"\'" );
	const bool delimIsNoWS = (params.csv_delim!=' ');
	const bool hasHeaderRepeat = (!params.header_repeat_mk.empty());
	const std::string filterID = (params.filter_ID.empty())? template_md.getStationID() : params.filter_ID; //necessary if filtering on stationID field
	const char comments_mk = params.comments_mk;
	Date prev_dt;
	while (!fin.eof()){
		getline(fin, line, params.eoln);
		linenr++;
		if (comments_mk!='\n') IOUtils::stripComments(line, comments_mk);
		if (params.purgeQuotes) IOUtils::removeQuotes(line);
		IOUtils::trim( line );
		if (line.empty()) continue; //Pure comment lines and empty lines are ignored
		if (hasHeaderRepeat && line.find(params.header_repeat_mk)!=std::string::npos) {
			FileUtils::skipLines(fin, params.header_lines);
			linenr += params.header_lines;
			continue;
		}
		
		const size_t nr_curr_data_fields = (delimIsNoWS)? IOUtils::readLineToVec(line, tmp_vec, params.csv_delim) : IOUtils::readLineToVec(line, tmp_vec);
		if (nr_of_data_fields==0) nr_of_data_fields = nr_curr_data_fields;
		
		//filter on ID
		if (params.ID_col!=IOUtils::npos) {
			if (tmp_vec.size()<=params.ID_col) { //we can not filter on the ID although it has been requested so we have to stop!
				std::ostringstream ss;
				ss << "File \'" << filename << "\' declares station ID in column " << params.ID_col << " but only has " << tmp_vec.size() << " columns at line ";
				ss << linenr << " :\n'" << line << "'\n";
				throw InvalidFormatException(ss.str(), AT);
			}
			
			if (tmp_vec[params.ID_col]!=filterID) continue;
		}
		
		//check that we have the expected number of fields
		if (nr_curr_data_fields!=nr_of_data_fields) {
			std::ostringstream ss;
			ss << "File \'" << filename << "\' declares (either as first data line or columns headers or units offset/multiplier) " << nr_of_data_fields << " columns ";
			ss << "but this does not match line " << linenr << " with " << nr_curr_data_fields << " fields :\n'" << line << "'\n";
			if (silent_errors) {
				std::cerr << ss.str();
				continue;
			} else throw InvalidFormatException(ss.str(), AT);
		}
		
		const Date dt( getDate(params, tmp_vec, silent_errors, filename, linenr) );
		if (dt.isUndef() && silent_errors) continue;

		if (linenr % streampos_every_n_lines == 0) {
			fpointer = fin.tellg();
			if (fpointer != static_cast<streampos>(-1)) indexer_map[filename].setIndex(dt, fpointer);
		}
		if (params.asc_order) {
			if (dt<dateStart) continue;
			if (dt>dateEnd) break;
		} else {
			if (dt<dateStart) break;
			if (dt>dateEnd) continue;
		}
		
		MeteoData md(template_md);
		md.setDate(dt);
		bool no_errors = true;
		for (size_t ii=0; ii<tmp_vec.size(); ii++){
			if (params.skip_fields.count(ii)>0) continue; //the user has requested this field to be skipped or this is a special field
			if (tmp_vec[ii].empty() || tmp_vec[ii]==nodata || tmp_vec[ii]==nodata_with_quotes || tmp_vec[ii]==nodata_with_single_quotes) //treat empty value as nodata, try nodata marker w/o quotes
				continue;
			
			if (tmp_vec[ii]=="NAN" || tmp_vec[ii]=="NULL") {
				md( params.csv_fields[ii] ) = IOUtils::nodata;
				continue;
			}
			
			double tmp;
			if (!IOUtils::convertString(tmp, tmp_vec[ii])) {
				const std::string err_msg( "Could not parse field '"+tmp_vec[ii]+"' in file \'"+filename+"' at line "+IOUtils::toString(linenr) );
				if (silent_errors) {
					std::cerr << err_msg << "\n";
					no_errors = false;
					continue;
				} else if (errors_to_nodata) {
					tmp = IOUtils::nodata;
				} else throw InvalidFormatException(err_msg, AT);
			}
			if (use_multiplier && tmp!=IOUtils::nodata) tmp *= params.units_multiplier[ii];
			if (use_offset && tmp!=IOUtils::nodata) tmp += params.units_offset[ii];
			md( params.csv_fields[ii] ) = tmp;
		}
		if (no_errors) vecMeteo.push_back( md );
	}

	if (!params.asc_order) std::reverse(vecMeteo.begin(), vecMeteo.end());
	
	return vecMeteo;
}

void CsvIO::readMeteoData(const Date& dateStart, const Date& dateEnd,
                             std::vector< std::vector<MeteoData> >& vecMeteo)
{
	vecMeteo.clear();
	vecMeteo.resize( csvparam.size() );
	for (size_t ii=0; ii<csvparam.size(); ii++) {
		vecMeteo[ii] = readCSVFile(csvparam[ii], dateStart, dateEnd);
	}
}

} //namespace
