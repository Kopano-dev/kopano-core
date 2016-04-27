<?php
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

?>
<?php
function getIncludes(){
	return array(
		"client/core/date.js",
		"client/layout/css/recurrence.css",
		"client/layout/css/date-picker.css",
		"client/layout/js/date-picker.js",
		"client/layout/js/date-picker-language.js",
		"client/layout/js/date-picker-setup.js",
		"client/layout/js/recurrence.js",
		"client/widgets/datetimepicker.js",
		"client/widgets/combineddatetimepicker.js",
		"client/widgets/datepicker.js",
		"client/widgets/timepicker.js");
}
			
function getDialogTitle() {
	if (get("taskrecurrence", false))
		return _("Task Recurrence");
	else
		return _("Appointment Recurrence");
}

function getJavaScript_onload(){ ?>
					getMailOptions();
					
					this.range_start = new datePicker("recstart",dhtml.getElementById("rangestart"), "<?=_("Start")?>");
					this.range_end = new datePicker("recend",dhtml.getElementById("rangeend"), "<?=_("End By")?>");
					this.appo_start = new timePicker(dhtml.getElementById("appostart"), "<?=_("Start")?>");
					this.appo_end = new timePicker(dhtml.getElementById("appoend"), "<?=_("End By")?>");
					this.taskrecurrence = <?=get("taskrecurrence", "false")?>;

					this.appo_start.onchange = eventApptStartTimePickerChange;
					this.appo_end.onchange = eventApptEndTimePickerChange;

					// Get recurrence info from parent window
					var recurrence = window.opener.module.getRecurrence();

					// enable remove recurrence button if recurrence is set.
					if(recurrence && recurrence.recurring)
						dhtml.getElementById("remove_recurrence").disabled = false;

					if(recurrence && recurrence.type) {
						showRecurrence(recurrence, this, taskrecurrence);
					} else {
						// Go to 'weekly' as default
						setRecurrenceType(11);

						// no recurrence set yet, start start & end to parent's start & end
						if (!taskrecurrence) {
							var start = new Date(window.opener.appoint_dtp.getStartValue() * 1000);
							var end = new Date(window.opener.appoint_dtp.getEndValue() * 1000);	

							// If event is set as an allday event then show it recurrence dialog too.
							dhtml.getElementById("allday_event").checked = window.opener.dhtml.getElementById("checkbox_alldayevent").checked;

							var tz = getTimeZone();
							if(tz)
								window.opener.module.setTimezone(tz);

							dhtml.addClassName(dhtml.getElementById("daily_regen_container"), "hideField");
							dhtml.addClassName(dhtml.getElementById("weekly_regen_container"), "hideField");
							dhtml.addClassName(dhtml.getElementById("monthly_regen_container"), "hideField");
							dhtml.addClassName(dhtml.getElementById("yearly_regen_container"), "hideField");
							dhtml.addClassName(dhtml.getElementById("weekly_everyn"), "hideField");

							this.appo_start.setValue(start.getHours() * 3600 + start.getMinutes() * 60);
							this.appo_end.setValue(end.getHours() * 3600 + end.getMinutes() * 60);
							
							// set start & end date range of recurrence as set in parent window.
							this.range_start.setValue(window.opener.appoint_dtp.getStartValue());
							this.range_end.setValue(window.opener.appoint_dtp.getEndValue());

						} else {
							// Hide Time range block
							dhtml.addClassName(dhtml.getElementById("time"), "hideField");

							var start = Date.parseDate(window.opener.dhtml.getElementById("text_commonstart").value, _("%d-%m-%Y"), true);
							var end = Date.parseDate(window.opener.dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), true);

							// startdate/duedate may be set
							if (!start) start = new Date();
							if (!end) end = new Date();

							// set start & end date range of recurrence as set in parent window.
							this.range_start.setValue(start.getTime()/1000);
							this.range_end.setValue(end.getTime()/1000);
						}

						var day = start.getDay();
						
						var dayelems = new Array('weekly_sunday', 'weekly_monday', 'weekly_tuesday', 'weekly_wednesday', 'weekly_thursday', 'weekly_friday', 'weekly_saturday');
						for(i=0;i<7;i++) {
							dhtml.getElementById(dayelems[i]).checked = (day == i);
						}

						dhtml.getElementById("monthly_dayn").value = start.getDate();
						dhtml.getElementById("yearly_mday").value = start.getDate();
						
						var monthstarts = new Array(0,44640,84960,129600,172800,217440,260640,305280,348480,393120,437760,480960);
						dhtml.getElementById("yearly_eachmonth").value = monthstarts[start.getMonth()];
						dhtml.getElementById("yearly_onmonth").value = monthstarts[start.getMonth()];

					}

					// Make sure the following fields cannot be set to 0
					dhtml.addEvent(-1, dhtml.getElementById("daily_ndays", "input"),"change",recurrenceOnInputChangePreventZero);
					dhtml.addEvent(-1, dhtml.getElementById("weekly_everynweeks", "input"),"change",recurrenceOnInputChangePreventZero);
					dhtml.addEvent(-1, dhtml.getElementById("monthly_everynmonths", "input"),"change",recurrenceOnInputChangePreventZero);
					dhtml.addEvent(-1, dhtml.getElementById("monthly_weekdayeverynmonths", "input"),"change",recurrenceOnInputChangePreventZero);
					dhtml.addEvent(-1, dhtml.getElementById("end_aftern", "input"),"change",recurrenceOnInputChangePreventZero);

					dhtml.addEvent(this, dhtml.getElementById("allday_event"), "change", eventRecurrenceAllDayEvent);
<?php } // getJavaScript_onload						

function getBody(){ ?>
<div>
	<fieldset id="time">
		<legend><?=_("Appointment time")?></legend>
		<div id="appostart" style="float: left"></div>
		<div id="appoend" style="float: left"></div>
		<div style="float: left">
			<input id="allday_event" type="checkbox" />
			<label for="allday_event"><?=_("All Day Event")?></label>
		</div>
	</fieldset>
	<fieldset id="pattern">
		<legend><?=_("Recurrence pattern")?></legend>
		<div class="right_border">
			<input onClick="setRecurrenceType(10);" type="radio" id="type_daily"   name="type" value="Daily" checked>
				<label for="type_daily"><?=_("Daily")?></label><br>
				
			<input onClick="setRecurrenceType(11);" type="radio" id="type_weekly"  name="type" value="Weekly">
				<label for="type_weekly"><?=_("Weekly")?></label><br>
				
			<input onClick="setRecurrenceType(12);" type="radio" id="type_monthly" name="type" value="Monthly">
				<label for="type_monthly"><?=_("Monthly")?></label><br>
				
			<input onClick="setRecurrenceType(13);" type="radio" id="type_yearly"  name="type" value="Yearly">
				<label for="type_yearly"><?=_("Yearly")?></label>
		</div>
		
		<div id="daily" class="pattern_type">
			<input type="radio" name="daysweekday" id="daily_everyndays" checked onClick="onChangeRecurPattern();">
				<label for="daily_everyndays"><?=_("Every")?></label>
				<input id="daily_ndays" type="text" value="1" size="3"> <?=_("day(s)")?><br>
				
			<input type="radio" name="daysweekday" id="daily_weekdays" onClick="onChangeRecurPattern();">
				<label for="daily_weekdays"><?=_("Every") . " " . _("weekday")?></label>

			<div id="daily_regen_container">
				<input type="radio" name="daysweekday" id="daily_regen" onClick="onChangeRecurPattern();">
				<label for="daily_regen"><?=_("Regenerate new task")?></label>
				<input id="daily_regen_ndays" type="text" value="1" size="3"> <?=_("day(s) after each task is completed")?>
			</div>
		</div>
		
		<div id="weekly" class="pattern_type" style="display:none">
			<input type="radio" name="weeklydays" id="weekly_everyn" checked onClick="onChangeRecurPattern();">
			<label for="weekly_everynweeks"><?=_("Every")?></label>
			<input id="weekly_everynweeks" type="text" value="1" size="3"> <?=_("week(s) on")?>:<br>
			
			<table>
				<tbody>
					<tr>
						<td rowspan="2">&nbsp;&nbsp;&nbsp;&nbsp;</td>
						<td><input id="weekly_monday" type="checkbox" name="Monday" checked><label for="weekly_monday"><?=_("Monday")?></label></td>
						<td><input id="weekly_tuesday" type="checkbox" name="Tuesday"><label for="weekly_tuesday"><?=_("Tuesday")?></label></td>
						<td><input id="weekly_wednesday" type="checkbox" name="Wednesday"><label for="weekly_wednesday"><?=_("Wednesday")?></label></td>
						<td><input id="weekly_thursday" type="checkbox" name="Thursday"><label for="weekly_thursday"><?=_("Thursday")?></label></td>
					</tr>
					<tr>
						<td><input id="weekly_friday" type="checkbox" name="Friday"><label for="weekly_friday"><?=_("Friday")?></label></td>
						<td><input id="weekly_saturday" type="checkbox" name="Saturday"><label for="weekly_saturday"><?=_("Saturday")?></label></td>
						<td><input id="weekly_sunday" type="checkbox" name="Sunday"><label for="weekly_sunday"><?=_("Sunday")?></label></td>
						<td></td>
					</tr>
				</tbody>
			</table>

			<div id="weekly_regen_container">
				<input type="radio" name="weeklydays" id="weekly_regen" onClick="onChangeRecurPattern();">
				<label for="weekly_regen"><?=_("Regenerate new task")?></label>
				<input id="weekly_regen_ndays" type="text" value="1" size="3"> <?=_("week(s) after each task is completed")?>
			</div>
		</div>
		
		<div id="monthly" class="pattern_type" style="display:none">
			<input type="radio" name="monthselect" id="monthly_ndayofmonth" checked onClick="onChangeRecurPattern();">
				<label for="monthly_ndayofmonth"><?=_("Day")?></label> 
				<input id="monthly_dayn" type="text" value="1" size="3"> <?=_("of every ")?> 
				<input id="monthly_everynmonths" type="text" value="1" size="3"> <?=_("month(s)")?><br>
				
			<input type="radio" name="monthselect" id="monthly_nweekdayofmonth" onClick="onChangeRecurPattern();">
				<label for="monthly_nweekdayofmonth"><?=_("The")?></label>
				<select id="monthly_nweekday">
					<option value="1"><?=_("1st")?></option>
					<option value="2"><?=_("2nd")?></option>
					<option value="3"><?=_("3rd")?></option>
					<option value="4"><?=_("4th")?></option>
					<option value="5"><?=_("last")?></option>
				</select>
				<select id="monthly_weekday">
					<option value="127"><?=_("Day")?></option>
					<option value="62"><?=_("Weekday")?></option>
					<option value="65"><?=_("Weekend day")?></option>
					<option value="2"><?=_("Monday")?></option>
					<option value="4"><?=_("Tuesday")?></option>
					<option value="8"><?=_("Wednesday")?></option>
					<option value="16"><?=_("Thursday")?></option>
					<option value="32"><?=_("Friday")?></option>
					<option value="64"><?=_("Saturday")?></option>
					<option value="1"><?=_("Sunday")?></option>
				</select>	
				<?=_("of every")?> 
				<input type="text" size="3" value="1" id="monthly_weekdayeverynmonths"> 
				<?=_("Month(s)")?>

			<div id="monthly_regen_container">
				<input type="radio" name="monthselect" id="monthly_regen" onClick="onChangeRecurPattern();">
				<label for="monthly_regen"><?=_("Regenerate new task")?></label>
				<input id="monthly_regen_ndays" type="text" value="1" size="3"> <?=_("month")."(s) "._("after each task is completed")?>
			</div>
		</div>
		
		<div id="yearly" class="pattern_type" style="display:none">
			<input name="yearlyradio" type="radio" id="yearly_month" checked onClick="onChangeRecurPattern();">
				<label for="yearly_month"><?=_("Every")?></label> 
				<select id="yearly_eachmonth">
					<option value="0"><?=_("January")?>
					<option value="44640"><?=_("February")?>
					<option value="84960"><?=_("March")?>
					<option value="129600"><?=_("April")?>
					<option value="172800"><?=_("May")?>
					<option value="217440"><?=_("June")?>
					<option value="260640"><?=_("July")?>
					<option value="305280"><?=_("August")?>
					<option value="348480"><?=_("September")?>
					<option value="393120"><?=_("October")?>
					<option value="437760"><?=_("November")?>
					<option value="480960"><?=_("December")?>
				</select>
				<input id="yearly_mday" type="text" value="1" size="3"><br>
				
			<input name="yearlyradio" type="radio" id="yearly_nthweekday" onClick="onChangeRecurPattern();">
				<label for="yearly_nthweekday"><?=_("The")?></label>
				<select id="yearly_nth">
					<option value="1"><?=_("1st")?></option>
					<option value="2"><?=_("2nd")?></option>
					<option value="3"><?=_("3rd")?></option>
					<option value="4"><?=_("4th")?></option>
					<option value="5"><?=_("last")?></option>
				</select>
				<select id="yearly_weekday">
                                        <option value="127"><?=_("Day")?></option>
                                        <option value="62"><?=_("Weekday")?></option>
                                        <option value="65"><?=_("Weekend day")?></option>
                                        <option value="2"><?=_("Monday")?></option>
                                        <option value="4"><?=_("Tuesday")?></option>
                                        <option value="8"><?=_("Wednesday")?></option>
                                        <option value="16"><?=_("Thursday")?></option>
                                        <option value="32"><?=_("Friday")?></option>
                                        <option value="64"><?=_("Saturday")?></option>
                                        <option value="1"><?=_("Sunday")?></option>
				</select>		
				<?=_("of")?>
				<select id="yearly_onmonth">
					<option value="0"><?=_("January")?>
					<option value="44640"><?=_("February")?>
					<option value="84960"><?=_("March")?>
					<option value="129600"><?=_("April")?>
					<option value="172800"><?=_("May")?>
					<option value="217440"><?=_("June")?>
					<option value="260640"><?=_("July")?>
					<option value="305280"><?=_("August")?>
					<option value="348480"><?=_("September")?>
					<option value="393120"><?=_("October")?>
					<option value="437760"><?=_("November")?>
					<option value="480960"><?=_("December")?>
				</select>

			<div id="yearly_regen_container">
				<input type="radio" name="yearlyradio" id="yearly_regen" onClick="onChangeRecurPattern();">
				<label for="yearly_regen"><?=_("Regenerate new task")?></label>
				<input id="yearly_regen_ndays" type="text" value="1" size="3"> <?=_("year(s) after each task is completed")?>
			</div>
		</div>
	</fieldset>
	<fieldset id="range">
		<legend><?=_("Range of recurrence")?></legend>
		<table>
			<tbody>
				<tr>
					<td valign="top">
						<div style="float: left;" id="rangestart"></div>
					</td>
					<td valign="top">
						<input type="radio" id="no_end" name="group2" value="no_end" checked><label for="no_end"><?=_("No end date")?></label><br>
						<input type="radio" id="end_after" name="group2" value="end_after"><label for="end_after"><?=_("End after")?>:</label><input id="end_aftern" type="text" value="10" size="3"> <?=_("occurrences")?><br>
						<input type="radio" id="end_by" name="group2" value="end_by" style="float: left;"><label for="end_by"><div id="rangeend"></div></label>
					</td>
				</tr>
			</tbody>
		</table>
	</fieldset>
	<br>
	<div class="dialog_buttons">
		<input type="button" style="width: 80px" value="<?=_("OK")?>" onClick="submitRecurrence(window);">
		<input type="button" style="width: 80px" value="<?=_("Cancel")?>" onClick="window.close();">
		<input id="remove_recurrence" disabled="true" type="button" value="<?=_("Remove Recurrence")?>" onClick="window.opener.module.setRecurrence();window.close();">
	</div>
</div>
<?php } // getBody
?>
