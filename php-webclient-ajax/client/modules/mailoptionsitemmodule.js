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

mailoptionsitemmodule.prototype = new ItemModule;
mailoptionsitemmodule.prototype.constructor = mailoptionsitemmodule;
mailoptionsitemmodule.superclass = ItemModule.prototype;

function mailoptionsitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

mailoptionsitemmodule.prototype.init = function(id)
{
	mailoptionsitemmodule.superclass.init.call(this, id);
}

mailoptionsitemmodule.prototype.item = function(action)
{
	var message = action.getElementsByTagName("item")[0];
	
	if(message && message.childNodes) {
		for(var i = 0; i < message.childNodes.length; i++)
		{
			var property = message.childNodes[i];
			
			if(property && property.firstChild && property.firstChild.nodeValue)
			{
				var element = dhtml.getElementById(property.tagName);
				var value = property.firstChild.nodeValue;
				if (property.tagName.toLowerCase()=="transport_message_headers"){
					value = value.htmlEntities();
					value = value.replace(/\r?\n/g, "<br>\n");
				}

				if(element) {
					switch(element.tagName.toLowerCase())
					{
						case "div":
							element.innerHTML = value;
							break;
						case "input":
							if (element.type == "checkbox"){
								if (parseInt(value)==1)
									element.checked = true;
								else
									element.checked = false;
							}else{
								element.value = value;
							}
							break;
						case "select":
							element.value = value;
							break;
					}
				}
			}
		}
		
		window.onresize();
	}
}

mailoptionsitemmodule.prototype.save = function(props, send, recipients, checknum)
{	
	delete props["read_receipt_requested"];
	mailoptionsitemmodule.superclass.save.call(this, props, send, recipients, checknum);
}
