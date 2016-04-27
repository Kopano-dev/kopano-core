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

/**
* Print class
*/
Print.prototype = new Widget;
Print.prototype.constructor = Print;
Print.superclass = Widget.prototype;

function Print()
{

}

Print.prototype.open = function(messageClass, data)
{
	this.messageClass = messageClass;
	this.data = data;
	
	var height = 300;
	var width = 400;
	modal("", "printpreview", "toolbar=0,location=0,status=0,menubar=0,resizable=0,width=" + width + ",height=" + height + ",top=" + ((screen.height / 2) - (height / 2)) + ",left=" + ((screen.width / 2) - (width / 2)));
	
	this.dialog = window.getWindowByName("printpreview");

	switch(this.messageClass) {
		case "ipm_note":
		default:
			this.messagePreview();
	}
}


Print.prototype.messagePreview = function()
{
	if(window.opener)
	{
		if(window.opener.top.main)
		{
			mail = window.opener.top.main.inbox_msgcontent.document;
			mail_content = window.opener.top.main.inbox_msgcontent.dhtml.getElementById('mail_content');
		}
		else
		{
			mail = window.opener.document;
			mail_content = window.opener.dhtml.getElementById('mail_content');
		}
	}
	
	if(mail_content && mail && mail_content.contentWindow.document.body && mail_content.contentWindow.document.body.innerHTML)
	{
		mail_header = document.createElement('div');
		
		//Subject
		subject = document.createElement('div');
		subject.innerHTML = mail.getElementById('mail_subject').innerHTML;
		subject.style.fontFamily = 'Arial';
		subject.style.color = '#000000';
		subject.style.fontSize = '12pt';
		subject.style.fontWeight = 'bold';
		mail_header.appendChild(subject);

		//Sender	
		sender = document.createElement('div');
		sender.innerHTML = mail.getElementById('mail_sender').innerHTML;
		sender.style.fontFamily = 'Arial';
		sender.style.color = '#000000';
		sender.style.fontSize = '9pt';
		sender.style.fontWeight = 'normal';
		mail_header.appendChild(sender);

		//Forwarded or Replied
		if(mail.getElementById('mail_forwarded_replied'))
		{
			forwarded = document.createElement('div');
			forwarded.innerHTML = mail.getElementById('mail_forwarded_replied').innerHTML;
			forwarded.style.backgroundColor = '#9B9AB3';
			forwarded.style.fontFamily = 'Arial';
			forwarded.style.color = '#ffffff';
			forwarded.style.fontSize = '8pt';
			forwarded.style.fontWeight = 'normal';
			forwarded.style.verticalAlign = 'middle';
			
			forwarded.style.paddingRight = '10px';
			forwarded.style.paddingLeft = '10px';
			forwarded.style.paddingTop = '2px';
			forwarded.style.paddingBottom = '2px';
			
			mail_header.appendChild(forwarded);
		}
		
		//To
		dl_tocc = document.createElement('table');
		row = document.createElement('tr');
		
		to = document.createElement('td');
		to.innerHTML = mail.getElementById('mail_info_to').innerHTML;
		to.style.fontFamily = 'Arial';
		to.style.color = '#666677';
		to.style.fontSize = '8pt';
		to.style.fontWeight = 'bold';
		to.style.width = '40px';
		to.style.verticalAlign = 'top';
		row.appendChild(to);
		
		//Receiver
		recipient = document.createElement('td');
		recipient.innerHTML = mail.getElementById('mail_recipient').innerHTML;
		recipient.style.fontFamily = 'Arial';
		recipient.style.color = '#000000';
		recipient.style.fontSize = '8pt';
		recipient.style.fontWeight = 'normal';
		row.appendChild(recipient);
		dl_tocc.appendChild(row);
		
		//CC
		if(mail.getElementById('mail_info_cc') && mail.getElementById('mail_cc'))
		{
			row = document.createElement('tr');
	
			//CC
			cc = document.createElement('td');
			cc.innerHTML = mail.getElementById('mail_info_cc').innerHTML;
			cc.style.fontFamily = 'Arial';
			cc.style.color = '#666677';
			cc.style.fontSize = '8pt';
			cc.style.fontWeight = 'bold';
			cc.style.width = '40px';
			cc.style.verticalAlign = 'top';
			row.appendChild(cc);
			
			//Receiver
			cc_recipient = document.createElement('td');
			cc_recipient.innerHTML = mail.getElementById('mail_cc').innerHTML;
			cc_recipient.style.fontFamily = 'Arial';
			cc_recipient.style.color = '#000000';
			cc_recipient.style.fontSize = '8pt';
			cc_recipient.style.fontWeight = 'normal';
			row.appendChild(cc_recipient);
			
			dl_tocc.appendChild(row);
		}
		mail_header.appendChild(dl_tocc);
		
		//Hr
		hr = document.createElement('hr');
		hr.style.color = '#666666';
		hr.style.height = '1px';
		mail_header.appendChild(hr);

		//Set the content
		document.body.innerHTML = mail_header.innerHTML + mail_content.contentWindow.document.body.innerHTML;
		
		//Print the document
    window.setTimeout('window.print()',250);
	}
}

