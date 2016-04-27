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

function addCategories()
{
	var available_categories = dhtml.getElementById("available_categories");
	var selected_categories = dhtml.getElementById("categories");
	
	var categories = selected_categories.value.split(";");

	for(var i = 0; i < available_categories.length; i++)
	{
		var option = available_categories.options[i];
		if(option.selected == true) {
			if(selected_categories.value.indexOf(";") > 0){
				if(selected_categories.value.indexOf(option.text + ";") < 0) 
					categories.push(option.text);
			}else{
				 if(selected_categories.value.indexOf(option.text) < 0)
				 	categories.push(option.text);
			}
		}
	}

	var tmpcategories = categories;
	categories = new Array();
	for(var i = 0; i < tmpcategories.length; i++) {
		if(tmpcategories[i] != "" && tmpcategories[i] != " ") {
			if(tmpcategories[i].indexOf(" ") == 0) {
				tmpcategories[i] = tmpcategories[i].substring(1);
			}
			
			categories.push(tmpcategories[i]);
		}
	}

	categories.sort(sortCategories);
	selected_categories.value = "";

	for(var i = 0; i < categories.length; i++) {
		selected_categories.value += categories[i] + "; ";
	}

	if (module)
		module.filtercategories(selected_categories, selected_categories.value, available_categories);
}

function sortCategories(a, b)
{
	if(a > b) return 1;
	if(a < b) return -1;
	return 0;
}
