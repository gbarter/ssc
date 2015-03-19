#include <math.h>

#include "common.h"
#include "core.h"

#include "lib_util.h"
#include "lib_battery.h"
#include "lib_weatherfile.h"

/* -------------------------------------
-------------------------------------- */

#ifndef M_PI
#define M_PI 3.141592653589793238462643
#endif

static var_info _cm_vtab_battery[] = {
	/*   VARTYPE           DATATYPE         NAME                      LABEL                              UNITS     META                      GROUP          REQUIRED_IF                 CONSTRAINTS                      UI_HINTS*/
	{ SSC_INPUT,		SSC_STRING,		"solar_resource_file",	"local weather file path",				"",			"",						"Weather",		"*",						"LOCAL_FILE",						"" },

	// generic battery inputs
	{ SSC_INPUT, SSC_NUMBER, "num_cells", "Number of Cells in Battery", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "battery_chemistry", "Battery Chemistry", "", "", "Battery", "", "", "" },

	// lead-acid inputs
	{ SSC_INPUT,		SSC_NUMBER,		"q20",					"Capacity at 20-hour discharge rate",	"Ah",		"",						"Battery",		"*",						"",									"" },
	{ SSC_INPUT,		SSC_NUMBER,		"q10",					"Capacity at 10-hour discharge rate",	"Ah",		"",						"Battery",		"*",						"",									"" },
	{ SSC_INPUT,		SSC_NUMBER,		"qn",					"Capacity at discharge rate for n-hour rate",	"Ah",		"",				"Battery",		"*",						"",									"" },
	{ SSC_INPUT,		SSC_NUMBER,		"tn",					"Time to discharge",					"h",		"",						"Battery",		"*",						"",									"" },
	{ SSC_INPUT,		SSC_NUMBER,		"R",					"Battery Internal Resistance",			"Ohm",		"",						"Battery",		"*",						"",									"" },

	// lithium-ion inputs
	{ SSC_INPUT, SSC_NUMBER, "Vfull", "Fully charged voltage", "V", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "Vexp", "Voltage at end of exponential zone", "V", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "Vnom", "Voltage at end of nominal zone", "V", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "Qfull", "Fully charged capacity", "Ah", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "Qexp", "Capacity at end of exponential zone", "Ah", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "Qnom", "Capacity at end of nominal zone", "Ah", "", "Battery", "*", "", "" },
	{ SSC_INPUT, SSC_NUMBER, "C_rate", "Rate at which voltage vs. capacity curve input", "", "", "Battery", "*", "", "" },


	// lifetime inputs
	{ SSC_INPUT,		SSC_ARRAY,		"DOD_vect",				"Depth of Discharge Curve Fit",			"",			"",						"Battery",		"*",						"",									"" },
	{ SSC_INPUT,		SSC_ARRAY,		"cycle_vect",			"Cycles to Failure Curve Fit",			"",			"",						"Battery",		"*",						"",									"" },
	
	// system energy and load
	{ SSC_INOUT, SSC_ARRAY, "hourly_energy", "Hourly energy", "kWh", "", "Time Series", "*", "", "" },
	{ SSC_INOUT, SSC_ARRAY, "e_load", "Electric load", "kWh", "", "Load Profile Estimator", "", "", "" },

	// storage dispatch
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p1.charge", "Period 1 Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p2.charge", "Period 2 Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p3.charge", "Period 3 Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p4.charge", "Period 4 Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p1.discharge", "Period 1 Discharging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p2.discharge", "Period 2 Discharging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p3.discharge", "Period 3 Discharging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p4.discharge", "Period 4 Discharging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p1.gridcharge", "Period 1 Grid Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p2.gridcharge", "Period 2 Grid Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p3.gridcharge", "Period 3 Grid Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_NUMBER,		"pv.storage.p4.gridcharge", "Period 4 Grid Charging Allowed?", "", "", "Battery", "*", "", "" },
	{ SSC_INPUT,		SSC_MATRIX,		"pv_storage_sched", "Battery Dispatch Schedule", "", "", "Battery", "*", "", "" },
	
	// economic inputs
	{ SSC_INOUT, SSC_NUMBER, "analysis_period", "Number of years in analysis", "years", "", "", "*", "INTEGER,POSITIVE", "" },
	{ SSC_INOUT, SSC_ARRAY, "degradation", "Annual energy degradation", "%", "", "AnnualOutput", "*", "", "" },
	{ SSC_INOUT, SSC_ARRAY, "load_escalation", "Annual load escalation", "%/year", "", "", "?=0", "", "" },
	{ SSC_INOUT, SSC_ARRAY, "rate_escalation", "Annual utility rate escalation", "%/year", "", "", "?=0", "", "" },
	{ SSC_INOUT, SSC_NUMBER, "inflation_rate", "Inflation rate", "%", "", "Financials", "*", "MIN=0,MAX=100", "" },

	// outputs, currently allows all subhourly.  May want to acculumulate up to hourly for most 
	{ SSC_OUTPUT, SSC_ARRAY, "q0", "Total Charge", "Ah", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "q1", "Available Charge", "Ah", "", "Battery", "", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "q2", "Bound Charge", "Ah", "", "Battery", "", "", "" },
	{ SSC_OUTPUT,       SSC_ARRAY,      "SOC",					"State of Charge",						"%",        "",						"Battery",       "*",						"",						"" },
	{ SSC_OUTPUT, SSC_ARRAY, "DOD", "Depth of Discharge", "%", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "qmaxI", "Max Capacity at Current", "Ah", "", "Battery", "", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "I", "Current", "A", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "voltage_cell", "Cell Voltage", "V", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "voltage_battery", "Battery Voltage", "V", "", "Battery", "*", "", "" },

	{ SSC_OUTPUT, SSC_ARRAY, "Damage", "Fractional Damage", "", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "Cycles", "Number of Cycles", "", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "battery_energy", "Power to/from Battery", "kWh", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "grid_energy", "Power from Grid to Battery", "kWh", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "Dispatch_mode", "Dispatch Mode for Hour", "", "", "Battery", "*", "", "" },
	{ SSC_OUTPUT, SSC_ARRAY, "Dispatch_profile", "Dispatch Profile for Hour", "", "", "Battery", "*", "", "" },



	var_info_invalid };

class cm_battery : public compute_module
{
public:

	cm_battery()
	{
		add_var_info(_cm_vtab_battery);
		// add_var_info(vtab_adjustment_factors);
	}

	void exec() throw(general_error)
	{
		/* **********************************************************************
		Read user specified system parameters from compute engine
		********************************************************************** */

		// generic battery properties
		int num_cells = as_integer("num_cells");
		int battery_chemistry = as_integer("battery_chemistry");

		// Lead acid battery properties 		
		double q20 = as_double("q20"); // [Ah]
		double q10 = as_double("q10"); // [Ah]
		double qn = as_double("qn"); //   [Ah]
		double I20 = q20 / 20; //	  [A]
		int tn = as_double("tn"); //	  [h]
		double R = as_double("R"); //		  [Ohm]

		// Lithium Ion properties
		double Vfull = as_double("Vfull");	   // [V]
		double Vexp = as_double("Vexp");	   // [V]
		double Vnom = as_double("Vnom");	   // [V]
		double Qfull = as_double("Qfull");	   // [Ah]
		double Qexp = as_double("Qexp");	   // [Ah]
		double Qnom = as_double("Qnom");	   // [Ah]
		double C_rate = as_double("C_rate");	   // [Ah]

		// Dispatch Timing Control
		size_t months = 12;
		size_t hours = 24;
		bool can_charge_array[4];
		bool can_discharge_array[4];
		bool grid_charge_array[4];
		
		can_charge_array[0] = as_boolean("pv.storage.p1.charge");
		can_charge_array[1] = as_boolean("pv.storage.p2.charge");
		can_charge_array[2] = as_boolean("pv.storage.p3.charge");
		can_charge_array[3] = as_boolean("pv.storage.p4.charge");
		can_discharge_array[0] = as_boolean("pv.storage.p1.discharge");
		can_discharge_array[1] = as_boolean("pv.storage.p2.discharge");
		can_discharge_array[2] = as_boolean("pv.storage.p3.discharge");
		can_discharge_array[3] = as_boolean("pv.storage.p4.discharge");
		grid_charge_array[0] = as_boolean("pv.storage.p1.gridcharge");
		grid_charge_array[1] = as_boolean("pv.storage.p2.gridcharge");
		grid_charge_array[2] = as_boolean("pv.storage.p3.gridcharge");
		grid_charge_array[3] = as_boolean("pv.storage.p4.gridcharge");
		ssc_number_t * pv_storage_schedule = as_matrix("pv_storage_sched", &months, &hours);
		util::matrix_t<float> schedule(12, 24);
		schedule.assign(pv_storage_schedule, months, hours);

		// if lead-acid KiBam, check capacity inputs
		if (battery_chemistry==0)
		{
			double capacities[] = { qn, q10, q20 };
			std::vector<double> vect_capacities(3);
			std::vector<double>::iterator it;
			it = std::unique_copy(capacities, capacities + 3, vect_capacities.begin());
			std::sort(vect_capacities.begin(), it);
			it = std::unique_copy(vect_capacities.begin(), it, vect_capacities.begin(), compare);
			vect_capacities.resize(std::distance(vect_capacities.begin(), it));
			if (vect_capacities.size() < 3) throw exec_error("battery", "Must enter at least 3 unique capacity values (20 hour, 10 hour, and one other)");
		}

		// lifetime inputs
		size_t numberOfPoints1, numberOfPoints2;
		std::vector<double> DOD_vect = as_doublevec("DOD_vect");
		std::vector<double> cycle_vect = as_doublevec("cycle_vect");
		numberOfPoints1 = DOD_vect.size();
		numberOfPoints2 = DOD_vect.size();
		if (numberOfPoints1 != numberOfPoints2) throw exec_error("battery", "Number of Cycles-to-Failure inputs must equal Depth-of-Discharge inputs");		

		/* **********************************************************************
		Ensure can handle subhourly
		********************************************************************** */
		const char *file = as_string("solar_resource_file");
		weatherfile wf(file);
		if (!wf.ok()) throw exec_error("battery", wf.error_message());
		size_t nrec = wf.nrecords;

		// check against actual inputs
		size_t len;
		ssc_number_t *hourly_energy = as_array("hourly_energy", &len);
		ssc_number_t *e_load = as_array("e_load", &len);

		if (len != nrec) 
			throw exec_error("battery", "Load and PV power do not match weatherfile length");

		size_t step_per_hour = nrec / 8760;
		if (step_per_hour < 1 || step_per_hour > 60 || step_per_hour * 8760 != nrec)
			throw exec_error("swh", util::format("invalid number of data records (%d): must be an integer multiple of 8760", (int)nrec));

		double ts_hour = 1.0 / step_per_hour;
		double ts_sec = 3600.0 / step_per_hour;

		/* **********************************************************************
		Initialize outputs
		********************************************************************** */
		ssc_number_t *outTotalCharge = allocate("q0", nrec);
		ssc_number_t *outAvailableCharge;
		ssc_number_t *outBoundCharge;
		ssc_number_t *outMaxChargeAtCurrent;

		// only allocate if lead-acid
		if (battery_chemistry==0)
		{
			outAvailableCharge = allocate("q1", nrec);
			outBoundCharge = allocate("q2", nrec);
			outMaxChargeAtCurrent = allocate("qmaxI", nrec);
		}

		ssc_number_t *outSOC = allocate("SOC", nrec);
		ssc_number_t *outDOD = allocate("DOD", nrec);
		ssc_number_t *outCurrent = allocate("I", nrec);
		ssc_number_t *outCellVoltage = allocate("voltage_cell", nrec);
		ssc_number_t *outBatteryVoltage = allocate("voltage_battery", nrec);
		ssc_number_t *outDamage = allocate("Damage", nrec);
		ssc_number_t *outCycles = allocate("Cycles", nrec);
		ssc_number_t *outBatteryEnergy = allocate("battery_energy", nrec);
		ssc_number_t *outGridEnergy = allocate("grid_energy", nrec); // Net grid energy required.  Positive indicates putting energy on grid.  Negative indicates pulling off grid
		ssc_number_t *outDispatchMode = allocate("Dispatch_mode", nrec);
		ssc_number_t *outDispatchProfile = allocate("Dispatch_profile", nrec);

		/* *********************************************************************************************
		Battery and Model Initialization
		*********************************************************************************************** */
		int mode = 0;
		double annual_wh = 0.0;
		size_t hour = 0;
		double SOC;
		double dt = ts_hour; 

		// HACK - Define in UI
		double dT = 0;

		// Component Models
		double other[] = { Vfull, Vexp, Vnom, Qfull, Qexp, Qnom, C_rate };
		voltage_dynamic_t VoltageModelDynamic(num_cells, Vnom, other);

		lifetime_t LifetimeModel(DOD_vect, cycle_vect, numberOfPoints1);
		capacity_kibam_t CapacityModelLeadAcid(q10, q20, I20, Vfull, tn, 10, qn, q10);
		capacity_lithium_ion_t CapacityModelLithiumIon(Qfull,Vfull);
		battery_t Battery;

		if (battery_chemistry==0)
			Battery.initialize(&CapacityModelLeadAcid, &VoltageModelDynamic, &LifetimeModel, dt);
		else if (battery_chemistry==1)
			Battery.initialize(&CapacityModelLithiumIon, &VoltageModelDynamic, &LifetimeModel, dt);

		// Component output
		output_map CapacityOutput;
		output_map VoltageOutput;
		output_map LifetimeOutput;
		/* *********************************************************************************************
		Storage Dispatch Initialization
		*********************************************************************************************** */
		int profile;
		bool can_charge, can_discharge, grid_charge;
		double p_grid=0.;			// energy needed from grid to charge battery.  Positive indicates sending to grid.  Negative pulling from grid.
		double p_tofrom_batt=0.;	// energy transferred to/from the battery.     Positive indicates discharging, Negative indicates charging
		int month;
		int curr_hour;

		// mode = 0: NO CHARGE, NO DISCHARGE
		// mode = 1: CHARGED ALL FROM GRID
		// mode = 2: CHARGED SOME FROM ARRAY, REST FROM GRID
		// mode = 3: CHARGED SOME FROM ARRAY, NONE FROM GRID
		// mode = 4: CHARGED ALL FROM ARRAY
		// mode = -1: DISCHARGED SOME TO MEET ALL OF LOAD
		// mode = -2: DISCHARGED ALL TO MEET SOME OF LOAD

		/* *********************************************************************************************
		Run Simulation
		*********************************************************************************************** */

		int count = 0;
		for (hour = 0; hour < 8760; hour++)
		{
			// Get Dispatch Settings for current hour
			// Remotely possible this could need to be updated for sub-hourly profiles
			getMonthHour(hour, &month, &curr_hour);
			profile = schedule.at(month - 1, curr_hour - 1);
			can_charge = can_charge_array[profile - 1];
			can_discharge = can_discharge_array[profile - 1];
			grid_charge = grid_charge_array[profile - 1];

			// Loop over subhourly
			for (size_t jj = 0; jj<step_per_hour; jj++)
			{

				// current charge state of battery from last time step.  
				double chargeNeededToFill = Battery.chargeNeededToFill();							// [Ah]
				double battery_voltage = Battery.batteryVoltage();									// [V]
				double powerNeededToFill = (chargeNeededToFill * battery_voltage)*watt_to_kilowatt;	// [kWh]
				double current_charge = Battery.getCurrentCharge();									// [Ah]
				double current_power = (current_charge * battery_voltage) *watt_to_kilowatt;		// [KWh]

				mode = 0; // NO CHARGE, NO DISCHARGE (can be overwritten)

				// Is there extra energy from array
				if (hourly_energy[count] > e_load[count])
				{
					if (can_charge)
					{
						if (hourly_energy[count] - e_load[count] > (powerNeededToFill))
						{
							p_tofrom_batt = -powerNeededToFill;
							hourly_energy[count] += p_tofrom_batt;
							mode = 4; // CHARGED ALL FROM ARRAY
						}
						else
						{
							// check if we want to charge from the grid
							if (grid_charge)
							{
								p_tofrom_batt = -powerNeededToFill;
								hourly_energy[count] += (-(hourly_energy[count] - e_load[count]));
								p_grid = powerNeededToFill - (hourly_energy[count] - e_load[count]);
								mode = 2; // CHARGED SOME FROM ARRAY, REST FROM GRID
							}
							else
							{
								p_tofrom_batt = -(hourly_energy[count] - e_load[count]);
								hourly_energy[count] += p_tofrom_batt;
								mode = 3; // CHARGED SOME FROM ARRAY
							}
						}
					}
					// if we want to charge from grid without charging from array
					else if (grid_charge)
					{
						p_tofrom_batt = -powerNeededToFill;
						p_grid = powerNeededToFill;
						mode = 1; // CHARGED ALL FROM GRID
					}

				}
				// Or, is the demand greater than or equal to what the array provides
				else if (e_load[count] >= hourly_energy[count])
				{
					if (can_discharge)
					{
						if (e_load[count] - hourly_energy[count] > current_power)
						{
							p_tofrom_batt = current_power;
							hourly_energy[count] += p_tofrom_batt;
							mode = -2; // DISCHARGED ALL TO MEET SOME OF LOAD
						}
						else
						{
							p_tofrom_batt = e_load[count] - hourly_energy[count];
							hourly_energy[count] += p_tofrom_batt;
							mode = -1; // DISCHARGED SOME TO MEET ALL OF LOAD
						}
					}
					// if we want to charge from grid
					else if (grid_charge)
					{
						p_tofrom_batt = -powerNeededToFill;
						p_grid = powerNeededToFill;
						mode = 1; // CHARGED ALL FROM GRID
					}
				}

				// Run Battery Model to update charge based on charge/discharge
				Battery.run(kilowatt_to_watt*p_tofrom_batt, dT);
				CapacityOutput = Battery.getCapacityOutput();
				LifetimeOutput = Battery.getLifetimeOutput();
				VoltageOutput = Battery.getVoltageOutput();

				// Capacity Output 
				if (battery_chemistry==0)
				{
					outAvailableCharge[count] = (ssc_number_t)(CapacityOutput["q1"]);
					outBoundCharge[count] = (ssc_number_t)(CapacityOutput["q2"]);
					outMaxChargeAtCurrent[count] = (ssc_number_t)(CapacityOutput["qmaxI"]);
				}
				outTotalCharge[count] = (ssc_number_t)(CapacityOutput["q0"]);
				outSOC[count] = (ssc_number_t)(CapacityOutput["SOC"]);
				outDOD[count] = (ssc_number_t)(CapacityOutput["DOD"]);
				outCurrent[count] = (ssc_number_t)(CapacityOutput["I"]);

				// Voltage Output
				outCellVoltage[count] = (ssc_number_t)(VoltageOutput["voltage_cell"]);
				outBatteryVoltage[count] = (ssc_number_t)(VoltageOutput["voltage_battery"]);

				// Lifetime Output
				outDamage[count] = (ssc_number_t)(LifetimeOutput["Damage"]);
				outCycles[count] = (int)(LifetimeOutput["Cycles"]);
				outBatteryEnergy[count] = p_tofrom_batt;
				outGridEnergy[count] = hourly_energy[count] - e_load[count];

				// Dispatch output
				outDispatchProfile[count] = profile;
				outDispatchMode[count] = mode;

				count++;
			}	// End loop over subhourly
		} // End loop over hourly



		// Have to finish up the lifetime model
		Battery.finish();
		LifetimeOutput = Battery.getLifetimeOutput();

		outDamage[count - 1] = (ssc_number_t)(LifetimeOutput["Damage"]);
		outCycles[count - 1] = (int)(LifetimeOutput["Cycles"]);
	}

};

DEFINE_MODULE_ENTRY(battery, "Battery storage standalone model .", 10)