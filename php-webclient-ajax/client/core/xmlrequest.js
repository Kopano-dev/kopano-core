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
 * XMLRequest
 * Responsible for sending and receiving XML requests. It sends XML to the
 * server and it receives XML from the server.
 *   
 * @param subsystem String identifying the subsystem doing the request
 * @todo
 * - Error report. If a request went wrong, report the module an error occured.
 *   The module can be found by the ID of the module.     
 */ 
function XMLRequest(subSystem)
{
	// All the requests, which are open
	this.requests = new Array();
	// XMLBuilder obejct
	this.xmlbuilder = new XMLBuilder();
	
	// Modules which should be deleted on the server
	this.deletemodules = new Array();

	this.hasData = false;
	this.hasDelay = false;
	this.requestCount = 0;
	this.subSystem = subSystem;
}

/**
 * Function which adds data to the XML, which will be send to the server.
 * @param object object reference to the module object
 * @param type string action type
 * @param object data data which will be send to the server in the <action> tag. (storeid, entryid, etc.)
 * @param string modulePrefix prefix for unique id    
 */ 
XMLRequest.prototype.addData = function(object, type, data, modulePrefix)
{
	// Create a module tag and an action tag
	var module = this.xmlbuilder.addModule(object.getModuleName(), (modulePrefix?modulePrefix + "_" + object.id:webclient.modulePrefix + "_" + object.id));
	var action = this.xmlbuilder.addAction(module, type);

	// Add data
	if(data) {
		if(typeof(data) == "string") {
			this.xmlbuilder.addReset();
		} else {
			this.xmlbuilder.addData(action, data);
		}
	}
	// Check if there are modules to be deleted on the server
	if(this.deletemodules.length > 0) {
		this.xmlbuilder.deleteModules(this.deletemodules);
		this.deletemodules = new Array();
	}

	this.hasData = true;
}

/**
 * Function which sends the request to the server.
 * @param boolean wait optional boolean which indicates if a timeout should be used
 */ 
XMLRequest.prototype.sendRequest = function(wait)
{
	if(this.hasData) {	
		// Create a request object
		var request = new XMLHttpRequest();
		
		if(request) {
			// Add request object to the request list
			this.requests.push(request);
				
			// Nasty time out used when window is closed (save an item)
			this.hasDelay = false;
			if(wait) {
				this.timeoutRequest = request;
				this.timeoutXML = this.xmlbuilder.getXML();
				window.setTimeout(sendRequest, 100);
				this.hasDelay = true;
			} else {
				// Send the request
				this.open(request, this.xmlbuilder.getXML());
			}

			// Reset the xml builder
			this.xmlbuilder.reset();
			this.hasData = false;
		}
	}
}

/**
 * Function which opens the request to the server.
 * @param object request XMLHttpRequest object
 * @param object xml the XML  
 */ 
XMLRequest.prototype.open = function(request, xml)
{
	// If timeout is used the arguments are not passed
	if(arguments.length == 0) {
		request = this.timeoutRequest;
		xml = this.timeoutXML;
	}
	
	if(request) {
		this.showLoader();
		request.open("POST", webclient.base_url+"zarafa.php?"+"subsystem="+this.subSystem, true);
		request.setRequestHeader("Content-Type", "text/xml;charset=utf-8;");
		request.onreadystatechange = this.readyStateChange;
		request.send(xml);
	}
}

/**
 * Function which will be executes if a response is received from the server.
 * It check if the request is completed and after that the XML is parsed. 
 */ 
XMLRequest.prototype.readyStateChange = function() 
{
	if ((typeof window.webclient != "undefined") && webclient.xmlrequest && webclient.xmlrequest.requests){
		var requests = webclient.xmlrequest.requests;
		
		// Loop through the requests, which are open
		for(var i in requests) {
			// Is the request completed?
			if(requests[i] && requests[i].readyState == 4) {
				// a request has returned, reset keep alive
				resetKeepAlive();
				
				// Only continue when we have a HTTP "200 Ok"
				if (typeof (requests[i].status)=="undefined" 
					|| requests[i].status != 200){

					var message = _("HTTP Error")+": ";

					// unknown error 0: could be "connection lost", so display that
					if (requests[i].status != 0)
						message += requests[i].status+" "+(requests[i].statusText==""?_("Unknown Error"):requests[i].statusText);
					else
						message += _("Connection to server lost");

					webclient.xmlrequest.showError(message);
					webclient.xmlrequest.hideLoader();
					webclient.xmlrequest.requests.splice(i, 1);
					continue; // ignore this request now, and continue to the next one
				}

				try {
					var xml = requests[i].responseXML;
					webclient.xmlrequest.requests.splice(i, 1);
					var modules = xml.getElementsByTagName(	"module");
				}catch(e){
					// we have an error in the XML here, or no XML at all
					// all "loading" modules must be informed about this,
					// for now, just show the browser error...
					webclient.xmlrequest.showError(_("Unknown server or connection problem"));

					/*
					if (e.description){
						webclient.xmlrequest.showError(e.description);
					}else{
						webclient.xmlrequest.showError(e.toString());
					}
					*/

					webclient.xmlrequest.hideLoader();
					continue; // ignore this request now, and continue to the next one
				}

				// check if webaccess needs a total reload (styles, languages etc)
				if (xml.getElementsByTagName("reload_webaccess").length == 1){
					webclient.reload();
				}

				// When the server returns an error, display it
				var error = xml.getElementsByTagName("error");
				if (error && error.length == 1 && error[0].parentNode.tagName == "zarafa"){
					var mapi = error[0].getAttribute("mapi");
					var logon = error[0].getAttribute("logon");
					var xml_error = error[0].getElementsByTagName("xml_error")[0];
					var message = dhtml.getTextNode(error[0], _("Unknown Error"));

					if (logon && logon=="false")
						message += "<br><a href=\""+webclient.base_url+"?logout\">"+_("Not logged on")+"</a>";
				
					if (mapi){
						message += "<br><span style=\"font-size: 6pt;\">";
						if (mapi.indexOf("0x")==0)
							message += "MAPI: "+ mapi;
						else
							message += "<br>"+mapi;
						message += "</span>";
					}

					if(xml_error) {
						message = xml_error.getElementsByTagName("message")[0].firstChild.nodeValue;
					}

					webclient.xmlrequest.showError(message);
				}

				for(var j = 0; j < modules.length; j++)
				{
					if (modules[j].parentNode.tagName!="zarafa"){
						continue;
					}

					// Name and ID
					var moduleName;
					var moduleID;
					
					var attributes = modules[j].attributes;
					for(var k = 0; k < attributes.length; k++)
					{
						var item = attributes.item(k);
						
						switch(item.nodeName)
						{
							case "name":
								moduleName = item.nodeValue;
								break;
							case "id":
								moduleID = item.nodeValue;
								break;
						}
					}
					
					// Get the actions 
					var actions = modules[j].getElementsByTagName("action");
					for(var k = 0; k < actions.length; k++)
					{
						if (actions[k].parentNode.tagName!="module"){
							continue;
						}

						var type = actions[k].getAttribute("type");
		
						// Get module
						var module = webclient.getModule(moduleID);
						
						if(module) {
							// Execute the action
							module.execute(type, actions[k]);
						}
					}
				}
				webclient.xmlrequest.hideLoader();
				
				// Modules may have requested more information 
				webclient.xmlrequest.sendRequest();
			}
		}
	}
	
}

XMLRequest.prototype.showError = function(msg)
{
	// show error message in a infobox, and keep the widget in memory for recycling
	if (typeof InfoBox != "undefined"){
		if (!this.errorbox){
			this.errorbox = new InfoBox(msg, 10000, "errorbox");
		}else{
			this.errorbox.show(msg);
		}
	}else{
		// It seems that InfoBox isn't loaded, so we use a normal alert box
		alert(msg);
	}
}

/**
 * Function which deletes a module on the server.
 * @param string moduleID the ID of the module 
 */ 
XMLRequest.prototype.deleteModule = function(moduleID)
{
	this.deletemodules.push(moduleID);
}

XMLRequest.prototype.showLoader = function()
{
	this.requestCount++;
	if(dhtml.getElementById("zarafa_loader")){
		dhtml.getElementById("zarafa_loader").style.visibility = "visible";
	}
}

XMLRequest.prototype.hideLoader = function()
{
	this.requestCount--;
	if(this.requestCount == 0 && dhtml.getElementById("zarafa_loader")){
		dhtml.getElementById("zarafa_loader").style.visibility = "hidden";
	}
}

/** 
 * Function which return the current request count.
 * @return int this.requestCount returnt the current request count.
 *
 */
XMLRequest.prototype.getRequestCount = function()
{
	return this.requestCount;
}
/**
 * Function which aborts all XML requests whoes response
 * is not yet received.
 */
XMLRequest.prototype.abortAll = function()
{
	this.requests = new Array();
}

/**
 * The timeout function to send a request with delay. It opens the request.
 */ 
function sendRequest()
{
	webclient.xmlrequest.open();
}


/**
 * Keep alive functions
 */

// needed in global space for the keepalive, to be sure we have just one timer at a time
var keepalive_timer = false; 

function startKeepAlive()
{
	if (!window.parentWebclient) { // only send keepalive when we are the main window, not a dialog with a parent webclient
		keepalive_timer = window.setInterval("sendKeepAlive()", webclient.settings.get("global/mail_check_timeout", CLIENT_TIMEOUT));
	}
}

function resetKeepAlive()
{
	if (keepalive_timer){
		window.clearInterval(keepalive_timer);
		keepalive_timer = false;
	}
	startKeepAlive();
}

function sendKeepAlive()
{
	var request = false;
	if (!webclient.xmlrequest.hasDelay) {
		// we can send the keep alive packet with this request
		request = webclient.xmlrequest;
	} else {
		// we can't use the default xmlrequest object because it is in use
		request = new XMLRequest();
	}

	if (request) {
		request.xmlbuilder.addKeepAlive();
		request.hasData = true;
		request.sendRequest();
	}
}

/**
* implement XMLHttpRequest object for Internet Explorer
*/
if (!window.XMLHttpRequest) {
	window.XMLHttpRequest = function()
	{
		if (window.ActiveXObject) {
			var types = ["MSXML2.XMLHTTP.6.0", "MSXML2.XMLHTTP.3.0"];
	
			for (var i = 0; i < types.length; i++) {
				try {
					return new ActiveXObject(types[i]);
				}
				catch(e) {}
			}
		}
		return undefined;
	}
}

/**
* Function to get a response header
*/
function getResponseHeader(request, header)
{
	var headers = request.getAllResponseHeaders().split("\n");
	for(var i=0;i<headers.length;i++){
		if (headers[i].substr(0,header.length+1)==header+":"){
			return headers[i].split(":")[1].trim();
		}
	}
	return undefined;
}
