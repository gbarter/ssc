#include "heliodata.h"

//helio data
helio_perf_data::helio_perf_data(){
	resetMetrics();
	n_metric = 13;
}

//vector<double*> *helio_perf_data::getDataVars(){
//	//Reset the pointer values
//	_dvars.clear();
//	_dvars.reserve(n_metric);
//	//POWER_TO_REC=0, ETA_TOT, ETA_COS, ETA_ATT, ETA_INT, ETA_BLOCK, ETA_SHADOW, POWER_VALUE, /* after this, order not significant */
//	//REFLECTIVITY, SOILING, REC_ABSORPTANCE, RANK_METRIC
//	_dvars.push_back(&power_to_rec);	//[W] delivered power
//	_dvars.push_back(&eta_tot);	//[-] Total heliostat intercept
//	_dvars.push_back(&eta_cos);	//[-] Heliostat cosine efficiency
//	_dvars.push_back(&eta_att);	//[-] Atmospheric attenuation efficiency
//	_dvars.push_back(&eta_int);	//[-] Intercept efficiency
//	_dvars.push_back(&eta_block);  //[-] Blocking efficiency
//	_dvars.push_back(&eta_shadow); //[-] Shadowing efficiency
//	_dvars.push_back(&power_value);	//Power weighted by the payment allocation factor if applicable
//	_dvars.push_back(&reflectivity);
//	_dvars.push_back(&soiling);
//	_dvars.push_back(&rec_absorptance);
//	_dvars.push_back(&rank_metric);
//	return &_dvars;
//}

double helio_perf_data::getDataByIndex(const int id){
	double rval;
	switch(id)
	{
		case helio_perf_data::POWER_TO_REC:
			rval = power_to_rec;
			break;
		case helio_perf_data::ETA_TOT:
			rval = eta_tot;
			break;
		case helio_perf_data::ETA_COS:
			rval = eta_cos;
			break;
		case helio_perf_data::ETA_ATT:
			rval = eta_att;
			break;
		case helio_perf_data::ETA_INT:
			rval = eta_int;
			break;
		case helio_perf_data::ETA_BLOCK:
			rval = eta_block;
			break;
		case helio_perf_data::ETA_SHADOW:
			rval = eta_shadow;
			break;
		case helio_perf_data::POWER_VALUE:
			rval = power_value;
			break;
		case helio_perf_data::REFLECTIVITY:
			rval = reflectivity;
			break;
		case helio_perf_data::SOILING:
			rval = soiling;
			break;
		case helio_perf_data::RANK_METRIC:
			rval = rank_metric;
			break;
		case helio_perf_data::REC_ABSORPTANCE:
			rval = rec_absorptance;
			break;
		case helio_perf_data::ETA_CLOUD:
			rval = eta_cloud;
			break;
		default:
			rval = 0.;
	}	
	return rval;
}

void helio_perf_data::setDataByIndex(const int id, double value){
	
	switch(id)
	{
		case helio_perf_data::ETA_ATT:
			eta_att = value;
			break;
		case helio_perf_data::ETA_BLOCK:
			eta_block = value;
			break;
		case helio_perf_data::ETA_COS:
			eta_cos = value;
			break;
		case helio_perf_data::ETA_INT:
			eta_int = value;
			break;
		case helio_perf_data::ETA_SHADOW:
			eta_shadow = value;
			break;
		case helio_perf_data::ETA_TOT:
			eta_tot = value;
			break;
		case helio_perf_data::REFLECTIVITY:
			reflectivity = value;
			break;
		case helio_perf_data::SOILING:
			soiling = value;
			break;
		case helio_perf_data::POWER_TO_REC:
			power_to_rec = value;
			break;
		case helio_perf_data::POWER_VALUE:
			power_value = value;
			break;
		case helio_perf_data::RANK_METRIC:
			rank_metric = value;
			break;
		case helio_perf_data::REC_ABSORPTANCE:
			rec_absorptance = value;
			break;
		case helio_perf_data::ETA_CLOUD:
			eta_cloud = value;
			break;
	}	
}

void helio_perf_data::resetMetrics(){
	eta_tot = 0.;
	eta_cos = 1.;
	eta_att = 1.;
	eta_int = 1.;
	eta_block = 1.;
	eta_shadow = 1.;
	eta_cloud = 1.;
	power_to_rec = 0.;
	power_value = 0.;
	rank_metric = 0.;
}

double helio_perf_data::calcTotalEfficiency(){
	eta_tot = eta_cos * eta_att * eta_int * eta_block * eta_shadow *reflectivity *soiling *eta_cloud; 
	return eta_tot;
}