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

function task_loadSettings(settings){
	var field;
	var value;

	// show completed tasks
	value = settings.get("tasks/show_completed", "true");
	field = dhtml.getElementById("preferences_tasks_showcomplete");
	field.checked = (value == "true");
}

function task_saveSettings(settings){
	var field;
	var old_value;

	// show completed tasks
	old_value = settings.get("tasks/show_completed", "true");
	field = dhtml.getElementById("preferences_tasks_showcomplete");	
	settings.set("tasks/show_completed", (field.checked) ? "true" : "false");
	if (old_value != (field.checked ? "true" : "false")){
		reloadNeeded = true;
	}
}