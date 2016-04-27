/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

function setRecurrenceType(type)
{
    var daily = dhtml.getElementById('daily');
    var weekly = dhtml.getElementById('weekly');
    var monthly = dhtml.getElementById('monthly');
    var yearly = dhtml.getElementById('yearly');
    
    var types = new Array(daily, weekly, monthly, yearly);
    var elems = new Array('type_daily', 'type_weekly', 'type_monthly', 'type_yearly');
    var i;
    
    for(i=0;i<4;i++) {
        types[i].style.display = (i == type - 10 ? '' : 'none');
    }

    dhtml.getElementById(elems[type-10]).checked = true;
}

function submitRecurrence()
{
	/**
	 * Split startdate and enddate string in to sub parts to get value
	 * German language  ->		dd.mm.yyyy
	 * English US language ->	dd/mm/yyyy
	 * Other languages  ->		dd-mm-yyyy
	 */
	var tmpStartDate = dhtml.getElementById("recstart_date").value.trim().split(/[-.//]/);

	var tmpEndDate = dhtml.getElementById("recend_date").value.trim().split(/[-.//]/);

	var startDate = new Date(parseInt(tmpStartDate[2],10), parseInt(tmpStartDate[1],10)-1, parseInt(tmpStartDate[0],10)).getTime();
	var endDate = new Date(parseInt(tmpEndDate[2],10), parseInt(tmpEndDate[1],10)-1, parseInt(tmpEndDate[0],10)).getTime();
	var currentDate = timeToZero(new Date().getTime()/1000)*1000;

	var submitResult = false;
    if(dhtml.getElementById("end_by").checked){
		if(startDate == endDate){
    		alert(_("The start date and end date for the range of recurrence can not be same."));  
		}else if(endDate < currentDate){
			if(confirm(_("All occurrences of this recurring appointment are scheduled in the past.\nWould you like to continue?")))
				submitResult = true;
		}else if(startDate > endDate){
    		alert(_("The recurrence pattern is not valid."));
		}else{
			submitResult = true;
		}
    }else{
		submitResult = true;
	}
	if(submitResult){
	    var result = retrieveRecurrence();
	    window.resultCallBack(result, window.callBackData);
	    window.close();
    }
}

// Retrieves the recurrence pattern by querying the view 'object'
function retrieveRecurrence() {
    var recurdata = new Object();

    if(dhtml.getElementById('end_by').checked) {
        recurdata.term = 0x21;
        recurdata.end = window.range_end.getValue(1);
    } else if(dhtml.getElementById('end_after').checked) {
        recurdata.term = 0x22;
        recurdata.numoccur = parseInt(dhtml.getElementById('end_aftern').value);
        recurdata.end = 79870662000; // 1-1-4501
    } else if(dhtml.getElementById('no_end').checked) {
        recurdata.term = 0x23;
        recurdata.end = 79870662000; // 1-1-4501
    }
    
    recurdata.start = window.range_start.getValue(1);
    recurdata.startocc = window.appo_start.getValue() / 60;
    recurdata.endocc = window.appo_end.getValue() / 60;
    
    // Recurrence from 23:00 to 01:00 should store 23*60->25*60, not 23*60->1*60
    if(recurdata.endocc < recurdata.startocc)
        recurdata.endocc += 24*60;
    
    /**
     * NOTE: alldayevent is not part of recurrence data,
     * but it can be set in recurrence window, so adding
     * to callBackData.
     */
    if(dhtml.getElementById('allday_event').checked) {
    	recurdata.alldayevent = 1;
    }
    
    if(dhtml.getElementById('type_daily').checked) {
        recurdata.type = 10;
		recurdata.regen = 0;

        if(dhtml.getElementById('daily_everyndays').checked) {
            recurdata.subtype = 0;
            recurdata.everyn = parseInt(dhtml.getElementById('daily_ndays').value, 10) * 1440;
        } else if(dhtml.getElementById('daily_weekdays').checked) {
            recurdata.subtype = 1;
            recurdata.everyn = 1;
		} else if (dhtml.getElementById('daily_regen').checked) {
			recurdata.subtype = 0;
			recurdata.regen = 1;
			recurdata.everyn = parseInt(dhtml.getElementById('daily_regen_ndays').value, 10) * 1440;
		}
    } else if(dhtml.getElementById('type_weekly').checked) {
		recurdata.type = 11;
		recurdata.regen = 0;
		if (dhtml.getElementById('weekly_regen').checked) {
			recurdata.subtype = 0;
			recurdata.regen = 1;
			recurdata.everyn = parseInt(dhtml.getElementById('weekly_regen_ndays').value, 10) * 7 * 1440;
		} else {
			recurdata.everyn = parseInt(dhtml.getElementById('weekly_everynweeks').value);
			recurdata.weekdays = 0;
			recurdata.subtype = 1; // always subtype 1 

			if(dhtml.getElementById('weekly_sunday').checked)
				recurdata.weekdays |= 1;
			if(dhtml.getElementById('weekly_monday').checked) 
				recurdata.weekdays |= 2;
			if(dhtml.getElementById('weekly_tuesday').checked) 
				recurdata.weekdays |= 4;
			if(dhtml.getElementById('weekly_wednesday').checked) 
				recurdata.weekdays |= 8;
			if(dhtml.getElementById('weekly_thursday').checked) 
				recurdata.weekdays |= 16;
			if(dhtml.getElementById('weekly_friday').checked) 
				recurdata.weekdays |= 32;
			if(dhtml.getElementById('weekly_saturday').checked) 
				recurdata.weekdays |= 64;
		}
    } else if(dhtml.getElementById('type_monthly').checked) {
        recurdata.type = 12;
		recurdata.regen = 0;
        if(dhtml.getElementById('monthly_ndayofmonth').checked) {
            recurdata.subtype = 2;
            recurdata.monthday = parseInt(dhtml.getElementById('monthly_dayn').value);
            recurdata.everyn = parseInt(dhtml.getElementById('monthly_everynmonths').value);
        } else if(dhtml.getElementById('monthly_nweekdayofmonth').checked) {
            recurdata.subtype = 3;
            recurdata.nday = parseInt(dhtml.getElementById('monthly_nweekday').value);
            recurdata.weekdays = parseInt(dhtml.getElementById('monthly_weekday').value);
            recurdata.everyn = parseInt(dhtml.getElementById('monthly_weekdayeverynmonths').value);
		} else  if (dhtml.getElementById('monthly_regen').checked) {
			recurdata.subtype = 2;
			recurdata.regen = 1;
			recurdata.everyn = parseInt(dhtml.getElementById('monthly_regen_ndays').value, 10);
			recurdata.monthday = 1;
		}
    } else if(dhtml.getElementById('type_yearly').checked) {
        recurdata.type = 13;
        recurdata.everyn = 1;
		recurdata.regen = 0;
        if(dhtml.getElementById('yearly_month').checked) {
            recurdata.subtype = 2;
            recurdata.month = parseInt(dhtml.getElementById('yearly_eachmonth').value);
            recurdata.monthday = parseInt(dhtml.getElementById('yearly_mday').value);
        } else if(dhtml.getElementById('yearly_nthweekday').checked) {
            recurdata.subtype = 3;
            recurdata.nday = parseInt(dhtml.getElementById('yearly_nth').value);
            recurdata.weekdays = parseInt(dhtml.getElementById('yearly_weekday').value);
            recurdata.month = parseInt(dhtml.getElementById('yearly_onmonth').value);
		} else if (dhtml.getElementById('yearly_regen').checked) {
			recurdata.subtype = 2;
			recurdata.regen = 1;
			recurdata.everyn = parseInt(dhtml.getElementById('yearly_regen_ndays').value, 10);
			recurdata.monthday = 1;
		}
    }
    
    return recurdata;
}

// Sets up the document 'object' to show the recurrence pattern in 'recurrence'
function showRecurrence(recurrence, object, isTaskRecurrence) {
	// Set daily/weekly/monthly/yearly
	setRecurrenceType(recurrence.type);

	// Set start date
	object.range_start.setValue(recurrence.start);

	// Set start/end time (this is stored and displayed in local time, so no conversion is done!)
	object.appo_start.setValue(recurrence.startocc * 60);
	object.appo_end.setValue(recurrence.endocc * 60);

	//set all day event
	if (recurrence["allday_event"]) {
		dhtml.setValue(dhtml.getElementById("allday_event"), true);
	}
	switch(recurrence.term) {
		case 0x21:
			dhtml.getElementById('end_by').checked = true;
			object.range_end.setValue(recurrence.end);
			break;
		case 0x22:
			dhtml.getElementById('end_after').checked = true;
			dhtml.getElementById('end_aftern').value = recurrence.numoccur;
			break;
		case 0x23:
			dhtml.getElementById('no_end').checked = true;
			break;
	}

	// Set all the knobs from the recurrence info					
	switch(recurrence.type) {
		case 10:
			// Daily
			if(recurrence.subtype == 1) {
				dhtml.getElementById('daily_weekdays').checked = true;
			} else {
				if (recurrence.regen) {
					dhtml.getElementById('daily_regen').checked = true;
					dhtml.getElementById('daily_regen_ndays').value = Math.floor(recurrence.everyn / 1440);
				} else {
					dhtml.getElementById('daily_everyndays').checked = true;
					dhtml.getElementById('daily_ndays').value = Math.floor(recurrence.everyn / 1440);
				}
			}
			break;
		case 11:
			// Weekly
			if (recurrence.regen) {
				dhtml.getElementById('weekly_regen').checked = true;
				dhtml.getElementById('weekly_regen_ndays').value = Math.floor(recurrence.everyn / (1440 * 7));
			} else {
				dhtml.getElementById('weekly_everynweeks').value = recurrence.everyn;
				dhtml.getElementById('weekly_everyn').checked = true;
				
				dhtml.getElementById('weekly_sunday').checked = recurrence.weekdays & 1;
				dhtml.getElementById('weekly_monday').checked = recurrence.weekdays & 2;
				dhtml.getElementById('weekly_tuesday').checked = recurrence.weekdays & 4;
				dhtml.getElementById('weekly_wednesday').checked = recurrence.weekdays & 8;
				dhtml.getElementById('weekly_thursday').checked = recurrence.weekdays & 16;
				dhtml.getElementById('weekly_friday').checked = recurrence.weekdays & 32;
				dhtml.getElementById('weekly_saturday').checked = recurrence.weekdays & 64;
			}
			break;
		case 12:
			// Monthly

			if (recurrence.regen) {
				dhtml.getElementById('monthly_regen').checked = true;
				dhtml.getElementById('monthly_regen_ndays').value = recurrence.everyn;
			} else {
				if(recurrence.subtype == 2) {
					// Day 15 of every 2 months
					dhtml.getElementById('monthly_ndayofmonth').checked = true;
					dhtml.getElementById('monthly_dayn').value = recurrence.monthday;
					dhtml.getElementById('monthly_everynmonths').value = recurrence.everyn;
				} else if(recurrence.subtype == 3) {
					// The 2nd tuesday of every 2 months
					dhtml.getElementById('monthly_nweekdayofmonth').checked = true;
					dhtml.getElementById('monthly_nweekday').value = recurrence.nday;
					dhtml.getElementById('monthly_weekday').value = recurrence.weekdays;
					dhtml.getElementById('monthly_weekdayeverynmonths').value = recurrence.everyn;
				}
			}
			break;
		case 13:
			// Yearly

			if (recurrence.regen) {
				dhtml.getElementById('yearly_regen').checked = true;
				dhtml.getElementById('yearly_regen_ndays').value = Math.floor(recurrence.everyn / 12);
			} else {
				if(recurrence.subtype == 2) {
					// Day 15 of February every 2 years
					dhtml.getElementById('yearly_month').checked = true;
					dhtml.getElementById('yearly_eachmonth').value = recurrence.month;
					dhtml.getElementById('yearly_mday').value = recurrence.monthday;
				} else if(recurrence.subtype == 3) {
					// 2nd tuesday in november every 2 years
					dhtml.getElementById('yearly_nthweekday').checked = true;
					dhtml.getElementById('yearly_nth').value = recurrence.nday;
					dhtml.getElementById('yearly_weekday').value = recurrence.weekdays;
					dhtml.getElementById('yearly_onmonth').value = recurrence.month;
				}
			}
			break;
	}

	if (!isTaskRecurrence) {
		dhtml.addClassName(dhtml.getElementById("daily_regen_container"), "hideField");
		dhtml.addClassName(dhtml.getElementById("weekly_regen_container"), "hideField");
		dhtml.addClassName(dhtml.getElementById("monthly_regen_container"), "hideField");
		dhtml.addClassName(dhtml.getElementById("yearly_regen_container"), "hideField");
		dhtml.addClassName(dhtml.getElementById('weekly_everyn'), "hideField");
	} else {
		dhtml.addClassName(dhtml.getElementById("time"), "hideField");
	}
}

function recurrenceOnInputChangePreventZero(moduleObject, element, event){
	if(parseInt(element.value, 10) == 0){
		element.value = "1";
	}
}

/**
 * Function which sets time 00:00 in timepickers
 * when alldayevent checkbox is checked.
 */
function eventRecurrenceAllDayEvent(moduleObject, element, event)
{
	if (element.checked){
		moduleObject.appo_start.setValue(0);
		moduleObject.appo_end.setValue(0);
	}else{
		var startValue =  moduleObject.appo_start.getValue();
		var newEndValue = startValue + (HALF_HOUR/1000);
		moduleObject.appo_end.setValue(newEndValue);
	}
}

function eventApptStartTimePickerChange(startTpObj, oldStartValue, startTpObjDayValue)
{
	var newStartValue = startTpObj.getValue();
	var oldEndValue = window.appo_end.getValue();
	var duration = oldEndValue - oldStartValue;

	var newEndValue = (duration > 0) ? (newStartValue + duration) : (newStartValue + (HALF_HOUR/1000));

	if (newStartValue != oldStartValue)
		startTpObj.setValue(newStartValue);

	if (newEndValue != oldEndValue)
		window.appo_end.setValue(newEndValue);

	if(dhtml.getElementById("allday_event").checked)
		dhtml.getElementById("allday_event").checked = false;
}

function eventApptEndTimePickerChange(endTpObj, oldEndValue, endTpObjDayValue)
{
	var newEndValue = endTpObj.getValue();
	var oldStartValue = window.appo_start.getValue();
	var duration = newEndValue - oldStartValue;

	if (newEndValue != oldEndValue)
		endTpObj.setValue(newEndValue);

	if (oldStartValue >= newEndValue && duration >= 0)
		window.appo_start.setValue(newEndValue - (HALF_HOUR/1000));

	if(dhtml.getElementById("allday_event").checked)
		dhtml.getElementById("allday_event").checked = false;
}

/**
 * Function which disables/enables when regenerate flag is selected/deselected
 */
function onChangeRecurPattern()
{
	var end_by = dhtml.getElementById('end_by');
	if (dhtml.getElementById('daily_regen').checked || dhtml.getElementById('weekly_regen').checked
		|| dhtml.getElementById('monthly_regen').checked || dhtml.getElementById('yearly_regen').checked) {
		end_by.disabled = true;
	} else {
		end_by.disabled = false;
	}
}