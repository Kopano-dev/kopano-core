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

function submitMailOptions()
{
    var importance = dhtml.getElementById("importance");
    var sensitivity = dhtml.getElementById("sensitivity");
    var read_receipt = dhtml.getElementById("read_receipt");
    
    var result = new Object;

    result.importance = parseInt(importance.options[importance.selectedIndex].value);
    result.sensitivity = parseInt(sensitivity.options[sensitivity.selectedIndex].value);
    result.readreceipt = read_receipt.checked;
    
    window.resultCallBack(result, window.callBackData);
}
