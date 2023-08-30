// SPDX-License-Identifier: LGPL-3.0-or-later
/***********************************************************************************/
/*  Copyright 2009 WSL Institute for Snow and Avalanche Research    SLF-DAVOS      */
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
#ifndef METEOPROCESSOR_H
#define METEOPROCESSOR_H

#include <meteoio/dataClasses/MeteoData.h>
#include <meteoio/Config.h>
#include <meteoio/Meteo1DInterpolator.h>
#include <meteoio/meteoFilters/ProcessingStack.h>

#include <vector>
#include <set>

namespace mio {

/**
 * @class MeteoProcessor
 * @brief A facade class that invokes the processing of the filters and the resampling
 * @author Thomas Egger
 * @date   2010-06-25
 */

class MeteoProcessor {
	public:
		/**
		 * @brief The default constructor - Set up a processing stack for each parameter
		 *        The different stacks are created on the heap and pointers to the objects
		 *        are stored in the map<string,ProcessingStack*> object processing_stack
		 * @param[in] cfg Config object that holds the config of the filters in the [Filters] section
		 * @param[in] rank in case of multiple TimeSeriesManager, rank in the stack? (default: 1)
		 * @param[in] mode spatial resampling operation mode (see IOUtils::OperationMode), default IOUtils::STD
		 */
		MeteoProcessor(const Config& cfg, const char& rank=1, const IOUtils::OperationMode &mode=IOUtils::STD);

		/**
		 * @brief The destructor - It is necessary because the ProcessingStack objects referenced in
		 *        the map<string, ProcessingStack*> processing_stack have to be freed from the heap
		 */
		~MeteoProcessor();

		/**
		 * @brief A function that executes all the filters for all meteo parameters
		 *        configuered by the user
		 * @param[in] ivec The raw sequence of MeteoData objects for all stations
		 * @param[in] ovec The filtered output of MeteoData object for all stations
		 * @param[in] second_pass Whether this is the second pass (check only filters)
		 */
		void process(std::vector< std::vector<MeteoData> >& ivec,
		             std::vector< std::vector<MeteoData> >& ovec, const bool& second_pass=false);

		bool resample(const Date& date, const std::string& stationHash, const std::vector<MeteoData>& ivec, MeteoData& md) {return mi1d.resampleData(date, stationHash, ivec, md);}
		
		void resetResampling() {mi1d.resetResampling();}

		void getWindowSize(ProcessingProperties& o_properties) const;

		const std::string toString() const;
		
		/**
		 * @brief built the set of station IDs that a filter should be applied to or excluded from
		 * @param[in] vecArgs All filter arguments
		 * @param[in] keyword Argument keyword (ex. EXCLUDE or ONLY)
		 * @return set of station IDs provided in argument by the provided keyword
		 */
		static std::set<std::string> initStationSet(const std::vector< std::pair<std::string, std::string> >& vecArgs, const std::string& keyword);
		
		/**
		 * @brief built the set of time ranges to apply a certain processing to
		 * @param[in] vecArgs All filter arguments
		 * @param[in] keyword Argument keyword (ex. WHEN)
		 * @param[in] where informative string to describe which component it is in case of error messages (ex. "Filter Min")
		 * @param[in] TZ time zone to use when building Date objects
		 * @return set of time ranges to apply the processing to
		 */
		static std::vector<DateRange> initTimeRestrictions(const std::vector< std::pair<std::string, std::string> >& vecArgs, const std::string& keyword, const std::string& where, const double& TZ);

 	private:
		static std::set<std::string> getParameters(const Config& cfg);
		static void compareProperties(const ProcessingProperties& newprop, ProcessingProperties& current);

		Meteo1DInterpolator mi1d;
		std::map<std::string, ProcessingStack*> processing_stack;
		bool enable_meteo_filtering;
};

/** 
 * @class RestrictionsIdx
 * @brief Convenience class for processing data with time restriction periods.
 * @details Given a vector of DateRange and a vector of MeteoData, compute which start/end indices
 * fit within the time restriction periods. Then repeatedly calling getStart() / getEnd() will provide
 * these indices while calling the \b ++ operator increment the time restriction period. 
 * Once isValid() returns false, there are no time restriction periods left.
 * @author Mathias Bavay
 */
class RestrictionsIdx {
	public:
		RestrictionsIdx() : start(), end(), index(IOUtils::npos) {}
		RestrictionsIdx(const METEO_SET& vecMeteo, const std::vector<DateRange>& time_restrictions);
		
		bool isValid() const {return (index != IOUtils::npos);}
		size_t getStart() const;
		size_t getEnd() const;
		RestrictionsIdx& operator++();
		const std::string toString() const;
		
	private:
		std::vector<size_t> start, end;
		size_t index;
};

} //end namespace

#endif
