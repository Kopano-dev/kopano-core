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
	require_once("class.xmlparser.php");
	require_once("class.xmlbuilder.php");
	require_once("class.dispatcher.php");
	
	/**
	* XML Request handler
	*
	* This class handles all incoming XML requests from the client. In short, it receives the XML,
	* does XML parsing, then sends the requests to the correct modules, and builds the reply XML. The reply
	* XML is then returned for reply.
	* @package core
	*/
	class Request
	{
		/**
		 * @var object the XMLParser object (PHP Class)
		 */
		var $xmlParser;
		
		/**
		 * @var object the XMLBuilder object (PHP Class)
		 */
		var $xmlBuilder;
		
		/**
		 * @var object the Dispatcher object (PHP Class)
		 */
		var $dispatcher;
		
		/**
		 * Constructor
		 */		 		
		function Request()
		{
			$this->xmlBuilder = new XMLBuilder();
		} 
		
		/**
		 * Execute incoming XML request
		 *
		 * This function executes the actions in the XML, which are received from
		 * the client. The entire XML request is processed at once here, and this function
		 * is therefore called only once for each HTTP request to the server.
		 *
		 * @param string $xml the xml string which is received by the client
		 * @return string the built xml which will be sent back to the client		 
		 * @todo Reduce overhead by not passing the entire XML data by string, but have the XMLParser parse data directly
		 *       from php://input
		 * @todo Reduce overhead by outputting created XML by outputting directly to php://output instead of returning a
		 *       (possibly huge) string containing the serialized XML
		 */
		function execute($xml)
		{
			// Create XMLParser object
			$this->xmlParser = new XMLParser(array("module", "action", "column", "recipient", "email_address", "attach_num"));
			// Parse the XML to an array
			$data = $this->xmlParser->getData($xml);
			// Create Dispatcher object
			$this->dispatcher = new Dispatcher();
			// Reset the bus
			$GLOBALS["bus"]->reset();
			
			// notify modules that wants to do something at the start of a request
			$GLOBALS["bus"]->notify(REQUEST_ENTRYID, REQUEST_START);

			// Check if the XML is parsed correctly into an array
			if(is_array($data)) {
				// Check if the client wants to reset the Bus and remove all the registered modules
				if(isset($data["reset"])) {
					$GLOBALS["bus"]->deleteAllRegisteredModules();
				}

				// Check if the "module" key exitsts
				if(isset($data["module"])) {
					// Loop through the modules
					foreach($data["module"] as $moduleData)
					{
						// Check if the attributes (id and name) isset
						if(isset($moduleData["attributes"]) && isset($moduleData["attributes"]["name"]) && isset($moduleData["attributes"]["id"]) && isset($moduleData["action"])) {
							// Module object
							$module = false;
	
							// Check if module already exists in the Bus
							if($module = $GLOBALS["bus"]->moduleExists($moduleData["attributes"]["id"])) {
								// Set Data
								$module->setData($moduleData["action"]);
							} else {
								// Create the module via the Dispatcher
								$module = $this->dispatcher->loadModule($moduleData["attributes"]["name"], $moduleData["attributes"]["id"], $moduleData["action"]);
							}

							// Check if the module is loaded
							if(is_object($module)) {
								// Execute the actions in the module
								if(!$module->execute()) {
									// TODO: error report.
								}
								
								// Clean up the data within the module.
								$module->reset();
								
								// Update the object in the bus, so all variables are still 
								// there if the object is called in the next request
								$GLOBALS["bus"]->setModule($module);
							}
						}
					}
				}

				// Check if the client wants to delete a module				
				if(isset($data["deletemodule"]) && isset($data["deletemodule"]["module"])) {
					// Delete the registered modules
					foreach($data["deletemodule"]["module"] as $moduleID)
					{
						$GLOBALS["bus"]->deleteRegisteredModule($moduleID);
					}
				}
				
				if (isset($data["request_webaccess_reload"])){
					// add "reload_webaccess" to XML output
					$GLOBALS["bus"]->responseData["reload_webaccess"] = true;
				}
			
			} else	{ // XML is not parsed correctly
				if(PEAR::isError($data)) {
					// return error to client
					$error = array();
					$error["error"] = array();
					$error["error"]["xml_error"] = array();
					// show user friendly error message only for end client
					// not for developers :)
					if(defined("DEBUG_XMLOUT")) {
						$error["error"]["xml_error"]["message"] = $data->getMessage();
					} else {
						$error["error"]["xml_error"]["message"] = _("Could not perform operation. Error in request") . ".";
					}
					$error["error"]["xml_error"]["error_code"] = $data->getCode();

					dump($data->toString());		// dump error for debugging
					return $this->xmlBuilder->build($error);
				}
				dump("Error in XML.");
			}

			// notify modules that wants to do something at the end of a request
			$GLOBALS["bus"]->notify(REQUEST_ENTRYID, REQUEST_END);
			
			// Build the XML and return it
			return $this->xmlBuilder->build($GLOBALS["bus"]->getData());
		}
	}
?>
