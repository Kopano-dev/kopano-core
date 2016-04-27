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

/**
* Conversion utility functions for converting various data types and structures.
*
* All functions in this class are 'static', although they are not marked as such for PHP4
* compatibility.
*
* @package core
*/
class Conversion {
    
	/**
	 * Convert hex-string to binary data
	 * @param string $data the hexadecimal string
	 */
	function hex2bin($data)
	{
		return pack("H*", $data);
	}

	/**
	* Convert from UTF-8 encoded string to Windows-1252 string
	*
	* Note that we try two different conversion methods:
	* 1. iconv
	* 2. php builtin 'utf8_decode'
	*
	* The latter option actually converts to iso-8859-1, which is slightly different
	* to windows-1252. Most importantly, iso-8859-1 does not support the Euro symbol. All
	* other characters in iso-8859-1 is compatible with windows-1252.
	*
	* @param string $string The UTF-8 string to convert
	* @return string Windows-1252 representation of the string
	*/
	function utf8_to_windows1252($string)
	{
		return $string;
	}

	/**
	* Convert from windows-1252 encoded string to UTF-8 string
	*
	* The same conversion rules as utf8_to_windows1252 apply.
	*
	* @see Conversion::utf8_to_windows1252()
	*
	* @param string $string the Windows-1252 string to convert
	* @return string UTF-8 representation of the string
	*/
	function windows1252_to_utf8($string)
	{
		return $string;
	}

	/**
	* Converts a COleDateTime binary string to an unix timestamp
	*
	* The time component of the returned timestamp is 00:00:00
	*
	* @param int $cOleDateTime is a binary string which represents the number of
	*               days since 30-12-1899.
	* @return int unix timestamp of given date
	*/
	function COleDateTimeToUnixTime($cOleDateTime)
	{
		$days = unpack("d", $cOleDateTime);
		$unixtime = ($days[1]-25569)*24*60*60; // 25569 is the number of days between 30-12-1899 and 01-01-1970
		return $unixtime;
	}
	
	/**
	* Converts an unix timestamp to a COleDateTime binary string
	*
	* The time component of the unix timestamp is discarded before conversion
	*
	* @param int $unixtime The unix timestamp to convert
	* @return int COleDateTime binary string representing the same day
	*/
	function UnixTimeToCOleDateTime($unixtime)
	{
		$days = ($unixtime/(60*60*24))+25569;
		$cOleDateTime = pack("d", $days);
		return $cOleDateTime;
	}
	
	/**
	 * Convert XML properties into MAPI properties
	 *
	 * This function converts an XML array to a MAPI array. For example:
	 * $props["subject"] = "Meeting" --> $props[PR_SUBJECT] = "Meeting"
	 *
	 * If a mapping between a property name ('subject') and a property tag (PR_SUBJECT) cannot be found,
	 * it is ignored and will not be returned in the return array.
	 * 		 		 		 	
	 * The return value of this function is suitable for direct use in mapi_setprops() or related functions.
	 * 
	 * Note that a conversion is done between utf-8 and windows-1252 here since all XML data is assumed to be
	 * utf-8 and all data written to MAPI is windows-1252.
	 *
	 * @param array $message_properties Properties to be converted
	 * @param array $props Mapping of property name to MAPI property tag
	 * @return array MAPI property array
	 *
	 * @todo This function is slow. Since it is used very very frequently, we could get some speedup from
	 *       optimisiing this function. The main problem is that it loops through '$props' instead of '$message_props'
	 *       which causes just a single property to still needs lots of iterations to be converted.
	 * @todo This function does more than just converting, most notably doing something with 'body'. This needs
	 *       commenting.
	 */
	function mapXML2MAPI($message_properties, $props)
	{
		$properties = array();
		
		foreach($props as $key => $value)
		{
			if(isset($message_properties[$key])) {
				$mapi_property = $message_properties[$key];

				switch($mapi_property){
					case PR_RULE_STATE:
						$states = explode("|",$value);
						$value = 0;
						foreach($states as $state){
							$state = trim($state);
							if (defined($state))
								$value += constant($state);
							else
								$value += intval($state);
						}
						break;
					case PR_SENT_REPRESENTING_SEARCH_KEY:
						$value = bin2hex($value);
						break;
				}

				switch(mapi_prop_type($mapi_property))
				{
					case PT_LONG:
						$properties[$mapi_property] = (int) $value;
						break;
					case PT_DOUBLE:
						if(settype($value, "double")) {
							$properties[$mapi_property] = $value;
						} else {
							$properties[$mapi_property] = (float) $value;
						}
						break;
					case PT_BOOLEAN:
						if(!is_bool($value)) {
							$properties[$mapi_property] = ($value=="0"||$value=="false"||$value=="-1"?false:true);
						} else {
							$properties[$mapi_property] = $value;
						}
						break;
					case PT_SYSTIME:
                        $properties[$mapi_property] = $value;
						break;
					case PT_MV_STRING8:
					case (PT_MV_STRING8 | MVI_FLAG):
						$mv_values = explode(";", $value);
						$values = array();
						
						foreach($mv_values as $mv_value)
						{
							if(!empty($mv_value)) {
								array_push($values, ltrim(utf8_to_windows1252($mv_value)));
							}
						}
						
						if(count($values) > 0) {
							$properties[($mapi_property &~ MV_INSTANCE)] = $values;
						}else{
							$properties[($mapi_property &~ MV_INSTANCE)] = array();
						}

						break;
					case PT_MV_BINARY:
						// TODO: not supported at the moment	
						break;
					case PT_STRING8:
						$properties[$mapi_property] = utf8_to_windows1252($value);
						break;
					case PT_BINARY:
						if (is_array($value) && isset($value["_content"])){
							$value = $value["_content"];
						}
						$properties[$mapi_property] = hex2bin($value);
						break;
					case PT_SRESTRICTION:
						$properties[$mapi_property] = Conversion::xml2restriction($value);
						break;
					case PT_ACTIONS:
						$properties[$mapi_property] = Conversion::xml2actions($value);
						break;
					default:
						$properties[$mapi_property] = $value;
						break;
				}
			} else if($key === "body") {
				$properties[PR_INTERNET_CPID] = 65001; // always write UTF-8 codepage
				if(isset($props["use_html"]) && $props["use_html"] == "true") {
					$properties[PR_HTML] = $value;
				}
				$properties[PR_BODY] = utf8_to_windows1252($value);
			}
		}

		return $properties;
	}
	
	/**
	 * Convert MAPI properties to XML array
	 *
	 * This function converts a MAPI property array to a XML array. For example:
	 * $props[PR_SUBJECT] = "Meeting" --> $props["subject"] = "Meeting"
	 * 
	 * <b>WARNING</b> Properties in PT_STRING8 are assumed to be in windows-1252. This is true 
     * for all PT_STRING8 properties in MAPI. The data in $props is UTF-8, so the conversion is done
	 * here.
	 *
	 * Conversion is done as the reverse of mapXML2MAPI.
	 * 		 		 		 		  
	 * @see Conversion::mapXML2MAPI()
	 * @param array $props List of MAPI properties
	 * @param array $message_properties MAPI array
	 * @return array list of properties which will be sent back to the client		  
	 * @todo This function is slow too, due to the same reason as stated in mapXML2MAPI()
	 * @todo This function does more than just conversion. The special handling for various
	 *       properties is a thorn in the eye...
	 */
	function mapMAPI2XML($props, $message_properties)
	{
		$properties = array();
		
		foreach($props as $name => $property)
		{
			if(isset($message_properties[($property &~ MVI_FLAG)])) {
				$property = ($property &~ MVI_FLAG);
			}
		
			if(isset($message_properties[$property])) {
				switch($property)
				{
					case PR_MESSAGE_SIZE:
						$message_properties[$property] = round($message_properties[$property] / 1024) . _("kb");
						break;
					case PR_MESSAGE_FLAGS:
						if(($message_properties[$property]&MSGFLAG_UNSENT)) {
							$properties["message_unsent"] = true;
						}
						break;
					case PR_RULE_STATE:
						$state = array();
						if ($message_properties[$property] & ST_ENABLED){
							$state[] = "ST_ENABLED";
						}else{
							$state[] = "ST_DISABLED";
						}
						if ($message_properties[$property] & ST_EXIT_LEVEL){
							$state[] = "ST_EXIT_LEVEL";
						}
						$message_properties[$property] = implode(" | ",$state);
						break;
				}
				
				$properties[$name] = Conversion::mapProp2XML($property, $message_properties[$property]);

			} else if($name == "body") {
				if(isset($message_properties[$property])) {
					$properties[$name] = $message_properties[$property];
				}
			}
		}

		return $properties;
	}
	
	/**
	 * Convert a single MAPI property into the XML value of the property
	 *
	 * @access private
	 * @param $property int MAPI property tag of the property
	 * @param $value mixed Value of the property
	 * @return array XML node of the converted property
	 */
	function mapProp2XML($property, $value)
	{
		$result = "";

		switch(mapi_prop_type($property))
		{
			case PT_BINARY:
				$result = array("attributes"=>array("type"=>"binary"),"_content"=>bin2hex($value));
				break;
			case PT_MV_BINARY:
				$val = array();
				$val["bin"] = array();
				foreach($value as $entry)
				{
					$val["bin"][] = bin2hex($entry);
				}
				$result = array("attributes"=>array("type"=>"binary", "count"=>count($value)),"_content"=>$val);
				break;
			case PT_MV_STRING8:
				$val = "";
				foreach($value as $entry)
				{
					if(!empty($entry)) {
						$val .= windows1252_to_utf8($entry) . "; ";
					}
				}
				
				$result = $val;
				break;
			case PT_BOOLEAN:
				$result = ($value? "1" : "0");
				break;
			case PT_SYSTIME:
				# NOTE:
				# This is a nice PHP bug. values smaller than this will produce a segfault in the strftime() call on 64bit systems.
				# So, we cap it on this number. I've only seen it in irrelevant properties (FlagDueBy), which have a PT_SYSTIME of 0,
				# which should represent 1-1-1601 00:00:00, which seems to be a default in a PST file?
				# on 32bit systems, this is in other manners broken, but atleast it doesn't segfault.
				if ($value < -6847761600)
					$value = -6847761600;
				$result = array();
				$result["attributes"] = array();
				$result["attributes"]["unixtime"] = $value;
				$result["attributes"]["type"] = "timestamp";
				$result["_content"] = $value;
				break;
			case PT_STRING8:
				// Note that we convert to utf8 here
				$result = windows1252_to_utf8($value);
				break;
			case PT_SRESTRICTION:
				$result = Conversion::restriction2xml($value);
				break;
			case PT_ACTIONS:
				$result = Conversion::actions2xml($value);
				break;
			default:
				// One-to-one mapping for all other types
				$result = $value;
				break;
		}
		return $result;	
	}

	/**
	 * Convert an XML structure into a MAPI SRestriction array
	 *
	 * @access private
	 * @param array $xml array The parsed XML array data
	 * @return array MAPI restriction array compatible with MAPI extension restriction format
	 * @see Conversion::restriction2xml
	 */
	function xml2restriction($xml)
	{
		if (!is_array($xml)){
			return $xml;
		}
		$restype = constant($xml["restype"]);
		$res = array();
		$res[0] = $restype;
		$res[1] = array();
		
		switch($xml["restype"]){
			case "RES_AND":
			case "RES_OR":
				if (isset($xml["restriction"]["restype"])){
					$xml["restriction"] = array($xml["restriction"]);
				}
				foreach($xml["restriction"] as $r){
					$res[1][] = Conversion::xml2restriction($r);
				}
				break;
			case "RES_NOT":
				$res[1][] = Conversion::xml2restriction($xml["restriction"]);
				break;

			case "RES_COMMENT":
				$res[1][PROPS] = array();
				foreach($xml["property"] as $prop){
					$res[1][PROPS][Conversion::propTagFromXML($prop)] = Conversion::propValFromXML($prop);
				}
				$res[1][RESTRICTION] = Conversion::xml2restriction($xml["restriction"]);
				break;
			case "RES_PROPERTY":
				$res[1][VALUE] = Conversion::getRestrictValueFromXML($xml);
				break;
			case "RES_CONTENT":
				$fl = explode("|",$xml["fuzzylevel"]);
				$res[1][FUZZYLEVEL] = 0;
				foreach($fl as $level){
					$res[1][FUZZYLEVEL] += constant(trim($level));
				}
				$res[1][VALUE] = Conversion::getRestrictValueFromXML($xml);
				break;
			case "RES_COMPAREPROPS":
				$res[1][ULPROPTAG1] = Conversion::propTagFromXML($xml["proptag1"]);
				$res[1][ULPROPTAG2] = Conversion::propTagFromXML($xml["proptag2"]);
				break;
			case "RES_BITMASK":
				$res[1][ULTYPE] = constant($xml["type"]);
				$res[1][ULMASK] = $xml["ulmask"];
				break;
			case "RES_SIZE":
				$res[1][CB] = $xml["cb"];
				break;
			case "RES_EXIST":
				break;
			case "RES_SUBRESTRICTION":
				$res[1][RESTRICTION] = Conversion::xml2restriction($xml["restriction"]);
				break;

			default:
		}

		if (isset($xml["proptag"]))
			$res[1][ULPROPTAG] = Conversion::propTagFromXML($xml["proptag"]);

		if (isset($xml["relop"]))
			$res[1][RELOP] = constant($xml["relop"]);


		return $res;
	}

	/**
	 * Convert a MAPI restriction to its XML representation
	 *
	 * @param array $res MAPI restriction array
	 * @return array XML representation of the restriction
	 * @see Conversion::xml2restriction()
	 */
	function restriction2xml($res)
	{
		// $res must be an array and must have 2 or more items
		if (!is_array($res) || count($res)<2){
			return $res;
		}
	
		$restriction = array();
	
		// first entry is always the type
		$restriction["restype"] = Conversion::getRestrictionName($res[0]);
		
		$data = array();
		
		switch ($res[0]){
			case RES_AND:
			case RES_OR:
			case RES_NOT:
				for($i=0;$i<count($res[1]);$i++){
					$data["restriction"][] = Conversion::restriction2xml($res[1][$i]);
				}
				break;
			
			default:
				if (isset($res[1][RELOP]))
					$data["relop"] = Conversion::getRelopName($res[1][RELOP]);
				
				if (isset($res[1][ULPROPTAG]))
					$data["proptag"] = Conversion::getPropertyName($res[1][ULPROPTAG]);
		
				if (isset($res[1][ULPROPTAG1]))
					$data["proptag1"] = Conversion::getPropertyName($res[1][ULPROPTAG1]);
		
				if (isset($res[1][ULPROPTAG2]))
					$data["proptag2"] = Conversion::getPropertyName($res[1][ULPROPTAG2]);
		
				if (isset($res[1][FUZZYLEVEL]))
					$data["fuzzylevel"] = Conversion::fuzzylevelToString($res[1][FUZZYLEVEL]);
	
				if (isset($res[1][ULTYPE])){
					switch($res[1][ULTYPE]){
						case BMR_NEZ:
							$data["type"] = "BMR_NEZ";
							break;
						case BMR_EQZ:
							$data["type"] = "BMR_EQZ";
							break;
						default:
							$data["type"] = $res[1][ULTYPE];
					}
				}

				if (isset($res[1][ULMASK]))
					$data["ulmask"] = $res[1][ULMASK];
	
				if (isset($res[1][CB]))
					$data["cb"] = $res[1][CB];
	
	
				if (isset($res[1][VALUE])){
					$value = reset($res[1][VALUE]);
					$key = key($res[1][VALUE]);
			
					$attributes = array("proptag"=>Conversion::getPropertyName($key));
			
					switch (mapi_prop_type($key)){
						case PT_BINARY:
							$value = bin2hex($value);
							$attributes["type"] = "binary";
     	    				$data["value"] = array("attributes"=>$attributes , "_content" => $value);
     	    				break;
                        default:
        					$data["value"] = array("attributes"=>$attributes , "_content" => w2u($value));
                    }
				}
				
				if (isset($res[1][RESTRICTION]))
					$data["restriction"] = Conversion::restriction2xml($res[1][RESTRICTION]);
				
				if (isset($res[1][PROPS])){
					$data["props"] = array();
					foreach($res[1][PROPS] as $tag=>$value){
						$attributes = array("proptag"=>Conversion::getPropertyName($tag));
			
						switch (mapi_prop_type($tag)){
							case PT_BINARY:
								$value = bin2hex($value);
								$attributes["type"] = "binary";
        						$data["property"][] = array("attributes"=>$attributes , "_content" => $value);
        						break;
                            default:
        						$data["property"][] = array("attributes"=>$attributes , "_content" => w2u($value));
        						break;
                        }
					}
				}
				break;
	
		}
	
		$restriction = array_merge_recursive($restriction, $data);
	
	
		return $restriction;
	}

	/**
    * Converts an XML 'actions' structure to a MAPI actions structure
    *
    * An 'actions' structure is used for PT_ACTIONS mapi types, which is used for rule actions.
    *
    * @access private
    * @param array $xml The XML array data
    * @return array the MAPI PT_ACTIONS value structure
    * @see Conversion::actions2xml()
    */
	function xml2actions($xml)
	{
		if (isset($xml["action"])) {
			$xml = array($xml);
		}
		$actionlist = array();
		foreach($xml as $xmlitem){
			$action = array();
			if (isset($xmlitem["action"]) && is_array($xmlitem["action"])){
				$xmlitem["action"] = $xmlitem["action"][0];
			}
			if (isset($xmlitem["action"]) && defined($xmlitem["action"])){
				$action["action"] = constant($xmlitem["action"]);
			}

			if (isset($xmlitem["flavor"])){
				$action["flavor"] = 0;
				$flavors = explode("|", $xmlitem["flavor"]);

				foreach($flavors as $flavor){
					$flavor = trim($flavor);
					if (defined($flavor)) {
						$action["flavor"] += constant($flavor);
					} else {
						$action["flavor"] += intval($flavor, 10);
					}
				}
			}

			if (isset($xmlitem["adrlist"])){
				$adrlist = array();
				foreach($xmlitem["adrlist"]["address"] as $address){
					$props = array();
					foreach($address as $prop){
						$props[Conversion::propTagFromXML($prop)] = Conversion::propValFromXML($prop);
					}
					$adrlist[] = $props;
				}
				$action["adrlist"] = $adrlist;
			}

			if (isset($xmlitem["proptag"])){
				/* FIXME ignoring actual value in $xmlitem */
				$action["proptag"][Conversion::propTagFromXML($xmlitem["proptag"])] = array();
			}

			$binaryentries = array("storeentryid", "folderentryid", "replyentryid", "replyguid");
			foreach($binaryentries as $entry){
				if (isset($xmlitem[$entry])){
					if (is_array($xmlitem[$entry])){
						$xmlitem[$entry] = $xmlitem[$entry]["_content"];
					}
					$action[$entry] = hex2bin($xmlitem[$entry]);
				}
			}


			$actionlist[] = $action;
		}
		return $actionlist;
	}
	
	/**
	* Converts a MAPI PT_ACTIONS value array to its XML representation
	*
	* @see Conversion::xml2actions()
	* @param $actions The MAPI actions value array from a PT_ACTIONS property
	* @return array The XML representation of the actions array
	*/
	function actions2xml($actions)
	{
		$xml = array();
		foreach($actions as $action){
			$xml_action = array();
			foreach($action as $key=>$val){
				switch($key){
					case "action":
						$xml_action["action"] = Conversion::getActionType($val);
						break;
					case "flavor":
						if ($action["action"]==OP_FORWARD||$action["action"]==OP_REPLY){
							$xml_action["flavor"] = Conversion::getFlavorName($val, $action["action"]);
						}
						break;
	
					case "storeentryid":
					case "folderentryid":
					case "replyentryid":
					case "replyguid":
						$xml_action[$key] = array("attributes"=>array("type"=>"binary"), "_content"=>bin2hex($val));
						break;

					case "adrlist":
						$adrlist = array();
						foreach($val as $adr){
							$proplist = array();
							foreach($adr as $proptag=>$propval){
								$xmlprop = Conversion::mapProp2XML($proptag, $propval);
								if (!is_array($xmlprop) || !isset($xmlprop["_content"])){
									$xmlprop = array("attributes"=>array(), "_content"=>$xmlprop);
								}
								$xmlprop["attributes"]["proptag"] = Conversion::getPropertyName($proptag);
								$proplist[] = $xmlprop;							
							}
							$adrlist[] = array("property"=>$proplist);
						}
						$xml_action["adrlist"] = array("address"=>$adrlist);
						break;
	
					case "proptag":
						if(is_array($val) && count($val) > 0) {
							// assume there will be only one proptag
							$tag = key($val);
							
							$xml_action["proptag"] = Conversion::mapProp2XML($tag, $val[$tag]);
							$xml_action["proptag"]["attributes"]["proptag"] = Conversion::getPropertyName($tag);
							
						}
						break;
					case "code":
					case "dam":
						// currently not used
						break;
				}
			//	$action[$key] = $val;
			}
			
			// Diplay hack: if we have a storeentryid and a folderentryid, get the folder name
			if(isset($action["storeentryid"]) && isset($action["folderentryid"]) && isset($GLOBALS["operations"])) {
				$xml_action["foldername"]["_content"] = w2u($GLOBALS["operations"]->getFolderName($action["storeentryid"], $action["folderentryid"]));
			}
			
			$xml[] = $xml_action;
		}
		return $xml;
	}
	
	/**
	* Convert the symbolic ACTION type to its numerical value
	*
	* @access private
	* @param $type string Type of the action (eg 'OP_MOVE')
	* @return int Type of the action (eg constant('OP_MOVE'))
	*/ 
	function getActionType($type)
	{
		switch($type){
			case OP_MOVE:
				return "OP_MOVE";
			case OP_COPY:
				return "OP_COPY";
			case OP_REPLY:
				return "OP_REPLY";
			case OP_OOF_REPLY:
				return "OP_OOF_REPLY";
			case OP_DEFER_ACTION:
				return "OP_DEFER_ACTION";
			case OP_BOUNCE:
				return "OP_BOUNCE";
			case OP_FORWARD:
				return "OP_FORWARD";
			case OP_DELEGATE:
				return "OP_DELEGATE";
			case OP_TAG:
				return "OP_TAG";
			case OP_DELETE:
				return "OP_DELETE";
			case OP_MARK_AS_READ:
				return "OP_MARK_AS_READ";
		}
		return $type;
	}

	/**
	* Get the string of the property name of the specified property tag
	* 
	* This function is used mainly for debugging and will return the symbolic
	* name of the specified property tag.
	*
	* Note that this depends on the definition of the property tag constants
	* in mapitags.php
	*
	* @example getPropertyName(0x0037001e) -> "PR_SUBJECT"
	* @param int MAPI property tag
	* @return string Symbolic name of the property tag
	*/
	function getPropertyName($proptag)
	{
		foreach(get_defined_constants() as $key=>$value){
			if (substr($key,0,3)=="PR_") {
				if ($proptag == $value){
					return $key;
				}
			}
		}
		return "0x".strtoupper(str_pad(dechex_32($proptag), 8, "0", STR_PAD_LEFT));
	}
	
	/**
	* Get the 'flavor name' of a forward action
	*
	* @access private
	* @param string $flavor string representation of the flavor from XML
	* @param Number $actionType Number representing action type (OP_REPLY or OP_FORWARD)
	* @return int MAPI flavor ID
	*/
	function getFlavorName($flavor, $actionType)
	{
		$flavors = array();

		if($actionType == OP_REPLY) {
			if (($flavor & STOCK_REPLY_TEMPLATE) === STOCK_REPLY_TEMPLATE)
				$flavors[] = "STOCK_REPLY_TEMPLATE";

			if (($flavor & DO_NOT_SEND_TO_ORIGINATOR) === DO_NOT_SEND_TO_ORIGINATOR)
				$flavors[] = "DO_NOT_SEND_TO_ORIGINATOR";
		} else if ($actionType == OP_FORWARD) {
			if (($flavor & FWD_PRESERVE_SENDER) === FWD_PRESERVE_SENDER)
				$flavors[] = "FWD_PRESERVE_SENDER";

			if (($flavor & FWD_DO_NOT_MUNGE_MSG) === FWD_DO_NOT_MUNGE_MSG)
				$flavors[] = "FWD_DO_NOT_MUNGE_MSG";

			if (($flavor & FWD_AS_ATTACHMENT) === FWD_AS_ATTACHMENT)
				$flavors[] = "FWD_AS_ATTACHMENT";
		}

		if(!empty($flavors)) {
			return implode(" | ", $flavors);
		} else {
			return $flavor;
		}
	}

	/**
	* Retrieves the data of a property value from an XML array structure
	*
	* Due to the way that the XML is built, the value for an XML property value
	* is not stored in a single location within the XML array structure. This function
	* gets the data from the correct place and does charset conversion if necessary based
	* on the property type.
	*
	* @access private
	* @param array $xmlprop array The XML array structure
	* @return mixed The 'value' part of the XML structure.
	* @todo This function is used a lot when parsing incoming XML. It could be speeded up a little
	*       by removing calls to u2w() when the value to be converted is not even a string.
	*/
	function propValFromXML($xmlprop)
	{
		if (!isset($xmlprop["_content"])){
			return "";
		}
		$value = $xmlprop["_content"];
		if (isset($xmlprop["attributes"]) && isset($xmlprop["attributes"]["type"])){
			switch($xmlprop["attributes"]["type"]){
				case "binary":
					$value = hex2bin($value);
					break;
				default:
					$value = u2w($value);
			}
		}else{
			$value = u2w($value);
		}
		return $value;
	}

	/**
	* Retrieves the property tag from an XML array structure
	*
	* The property tag can be located in various places and this function searches for the location of the property tag
	* and returns that value
	*
	* @access private
	* @param array $xmlprop The XML array structure
	* @return int The MAPI property tag (eg 0x0037001e)
	*/
	function propTagFromXML($xmlprop)
	{
		if(is_array($xmlprop) && isset($xmlprop["attributes"]) && isset($xmlprop["attributes"]["proptag"])){
			$tag = $xmlprop["attributes"]["proptag"];
		}else{
			$tag = $xmlprop;
		}
		if (defined($tag)) {
			$tag = (int) constant($tag);
		}else{
			$tag = (int) hexdec($tag);
		}
		return $tag;
	}

	/**
	* Gets the restriction value from an XML array structure
	*
	* @access private
	* @param array $xml the XML array structure
	* @return array MAPI restriction array
	*/
	function getRestrictValueFromXML($xml)
	{
		$result = "";
		$proptag = Conversion::propTagFromXML($xml["value"]);
		if ($proptag == 0 && isset($xml["proptag"])) {
			$proptag = Conversion::propTagFromXML($xml["proptag"]);
		}
		if ($proptag != 0){
			$result = array($proptag => Conversion::propValFromXML($xml["value"]));
		}else{
			$result = Conversion::propValFromXML($xml["value"]);
		}

		return $result;
	}
	
	/**
	* Gets the RELOP type from a string RELOP type
	*
	* @access private
	* @param $relop_type string The string representing the RELOP type
	* @return int The RELOP type to be used in a MAPI restriction structure
	*/
	function getRelopName($relop_type)
	{
		foreach(get_defined_constants() as $key=>$value){
			if (substr($key,0,6)=="RELOP_") {
				if ($relop_type == $value){
					return $key;
				}
			}
		}
		return $relop_type;
	}

	/**
	* Gets the restriction type name from an XML restriction type string
	*
	* @access private
	* @param $res_type string The string represting the XML restriction type
	* @return int The restriction type to be used in a MAPI restriction structure
	*/
	function getRestrictionName($res_type)
	{
		foreach(get_defined_constants() as $key=>$value){
			if (substr($key,0,4)=="RES_") {
				if ($res_type == $value){
					return $key;
				}
			}
		}
		return $res_type;
	}

	/**
	* Gets the 'fuzzylevel' string to be sent in XML from a MAPI fuzzylevel
	*
	* @access private
	* @param int $level
	* @return string Fuzzylevel to be sent in XML when sending a restriction
	*/
	function fuzzylevelToString($level)
	{
		$levels = array();

		if (($level & FL_SUBSTRING) == FL_SUBSTRING)
			$levels[] = "FL_SUBSTRING";
		elseif (($level & FL_PREFIX) == FL_PREFIX)
			$levels[] = "FL_PREFIX";
		else
			$levels[] = "FL_FULLSTRING";
					
		if (($level & FL_IGNORECASE) == FL_IGNORECASE)
			$levels[] = "FL_IGNORECASE";
					
		if (($level & FL_IGNORENONSPACE) == FL_IGNORENONSPACE)
			$levels[] = "FL_IGNORENONSPACE";
		
		if (($level & FL_LOOSE) == FL_LOOSE)
			$levels[] = "FL_LOOSE";
		
		return implode(" | ",$levels);
	}

	/******** JSON related functions **********/

	/**
	 * Convert an JSON restriction structure into a MAPI SRestriction array
	 *
	 * @access		private
	 * @param		array		$json		The parsed JSON array data
	 * @return		array					MAPI restriction array compatible with MAPI extension restriction format
	 */
	function json2restriction($json)
	{
		if (!is_array($json)){
			return $json;
		}

		switch($json[0]){
			case RES_AND:
			case RES_OR:
				if(isset($json[1][0]) && is_array($json[1][0])) {
					foreach(array_keys($json[1]) as $key) {
						$json[1][$key] = Conversion::json2restriction($json[1][$key]);
					}
				} else if(isset($json[1]) && $json[1]) {
					$json[1] = Conversion::json2restriction($json[1]);
				}
				break;
			case RES_NOT:
				$json[1][0] = Conversion::json2restriction($json[1][0]);
				break;
			case RES_COMMENT:
				foreach(array_keys($json[1][PROPS]) as $key){
					$prop = &$json[1][PROPS][$key];		// PHP 4 does not support pass by reference in foreach loop
					$propTag = $prop[ULPROPTAG];

					if(!isset($prop[VALUE][$propTag])) {
						/**
						 * multi valued properties uses single valued counterpart in
						 * VALUES part, so use it get values
						 */
						$propTagForValue = Conversion::convertToSingleValuedProperty($propTag);
						$propValue = $prop[VALUE][$propTagForValue];
						
						// Empty MV property (cValues = 0)
						if(!isset($propValue)) {
							$propValue = array();
						}
					} else {
						$propTagForValue = $propTag;
						$propValue = $prop[VALUE][$propTag];
					}

					if(isset($json[1][VALUE]["type"]) && $json[1][VALUE]["type"] == "binary") {
						// convert hex value to binary
						$propValue = hex2bin($propValue);
					} else if(is_string($propValue)) {
						// convert utf-8 to windows-1252
						$propValue = u2w($propValue);
					}

					$propTag = Conversion::propTagFromJSON($propTag);
					$propTagForValue = Conversion::propTagFromJSON($propTagForValue);

					// remove non restriction data
					unset($prop[VALUE]);

					$prop[ULPROPTAG] = $propTag;
					$prop[VALUE] = array();
					$prop[VALUE][$propTagForValue] = $propValue;
				}
				$json[1][RESTRICTION] = Conversion::json2restriction($json[1][RESTRICTION]);
				break;
			case RES_PROPERTY:
			case RES_CONTENT:
				$propTag = $json[1][ULPROPTAG];

				if(!isset($json[1][VALUE][$propTag])) {
					/**
					 * multi valued properties uses single valued counterpart in
					 * VALUES part, so use it to get values
					 */
					$propTagForValue = Conversion::convertToSingleValuedProperty($propTag);
					$propValue = $json[1][VALUE][$propTagForValue];
				} else {
					$propTagForValue = $propTag;
					$propValue = $json[1][VALUE][$propTag];
				}

				if(isset($json[1][VALUE]["type"]) && $json[1][VALUE]["type"] == "binary") {
					// convert hex value to binary
					$propValue = hex2bin($propValue);
				} else if(is_string($propValue)) {
					// convert utf-8 to windows-1252
					$propValue = u2w($propValue);
				}

				$propTag = Conversion::propTagFromJSON($propTag);
				$propTagForValue = Conversion::propTagFromJSON($propTagForValue);

				// remove non restriction data
				unset($json[1][VALUE]);

				$json[1][ULPROPTAG] = $propTag;
				$json[1][VALUE] = array();
				$json[1][VALUE][$propTagForValue] = $propValue;
				break;
			case RES_COMPAREPROPS:
				$json[1][ULPROPTAG1] = Conversion::propTagFromJSON($json[1][ULPROPTAG1]);
				$json[1][ULPROPTAG2] = Conversion::propTagFromJSON($json[1][ULPROPTAG2]);
				break;
			case RES_BITMASK:
			case RES_SIZE:
			case RES_EXIST:
				$json[1][ULPROPTAG] = Conversion::propTagFromJSON($json[1][ULPROPTAG]);
				break;
			case RES_SUBRESTRICTION:
				$json[1][ULPROPTAG] = Conversion::propTagFromJSON($json[1][ULPROPTAG]);
				$json[1][RESTRICTION] = Conversion::json2restriction($json[1][RESTRICTION]);
				break;
		}

		return $json;
	}

	/**
	* Retrieves the property tag from a JSON array structure
	*
	* The property tag can be located in various places and this function 
	* searches for the location of the property tag and returns that value
	*
	* @param		array		$propTag		The JSON array structure
	* @return		int							The MAPI property tag (eg 0x0037001e)
	*/
	function propTagFromJSON($propTag)
	{
		if(defined($propTag)) {
			$propTag = constant($propTag);
		} else {
			$propTag = hexdec($propTag);
		}

		if($propTag >= PHP_INT_MAX) {
			/**
			 * arrays in php can have only string or integer index and 
			 * integer value can not be greater than PHP_INT_MAX value
			 * so if we are crossing integer boundary when converting
			 * property tag to integer value then we are manually
			 * typecasting it to integer so we can use it in arrays
			 */
			$propTag = (int) $propTag;
		}

		return $propTag;
	}

	/**
	* Retrieves singlevalued counterpart of a multivalued property
	*
	* Multivalued properties has different property tags in VALUES part
	* so we need to find that singlevalued property tag
	*
	* here we can't do bitwise operation because property tags are passed
	* as a string from client ($propTag ^ MV_FLAG)
	*
	* @param		String		$propTag		The multivalued property tag in string
	* @return		String						The singlevalued property tag
	*/
	function convertToSingleValuedProperty($propTag)
	{
		// get property type from property tag
		$propType = substr($propTag, 6);
		$propId = substr($propTag, 0, 6);

		$propTag = $propId . "0" . substr($propType, 1);

		return $propTag;
	}
	
	/**
	 * Get charset name from a codepage
	 *
	 * @see http://msdn.microsoft.com/en-us/library/dd317756(VS.85).aspx
	 *
	 * Table taken from common/codepage.cpp
	 *
	 * @param integer codepage Codepage
	 * @return string iconv-compatible charset name
	 */
	function getCodepageCharset($codepage)
	{
		$codepages = array(
			20106 => "DIN_66003",
			20108 => "NS_4551-1",
			20107 => "SEN_850200_B",
			950 => "big5",
			50221 => "csISO2022JP",
			51932 => "euc-jp",
			51936 => "euc-cn",
			51949 => "euc-kr",
			949 => "euc-kr",
			936 => "gb18030",
			52936 => "csgb2312",
			852 => "ibm852",
			866 => "ibm866",
			50220 => "iso-2022-jp",
			50222 => "iso-2022-jp",
			50225 => "iso-2022-kr",
			1252 => "windows-1252",
			28591 => "iso-8859-1",
			28592 => "iso-8859-2",
			28593 => "iso-8859-3",
			28594 => "iso-8859-4",
			28595 => "iso-8859-5",
			28596 => "iso-8859-6",
			28597 => "iso-8859-7",
			28598 => "iso-8859-8",
			28599 => "iso-8859-9",
			28603 => "iso-8859-13",
			28605 => "iso-8859-15",
			20866 => "koi8-r",
			21866 => "koi8-u",
			932 => "shift-jis",
			1200 => "unicode",
			1201 => "unicodebig",
			65000 => "utf-7",
			65001 => "utf-8",
			1250 => "windows-1250",
			1251 => "windows-1251",
			1253 => "windows-1253",
			1254 => "windows-1254",
			1255 => "windows-1255",
			1256 => "windows-1256",
			1257 => "windows-1257",
			1258 => "windows-1258",
			874 => "windows-874",
			20127 => "us-ascii"
		);
		
		if(isset($codepages[$codepage])) {
			return $codepages[$codepage];
		} else {
			// Defaulting to iso-8859-15 since it is more likely for someone to make a mistake in the codepage
			// when using west-european charsets then when using other charsets since utf-8 is binary compatible
			// with the bottom 7 bits of west-european
			return "iso-8859-15";
		}
	}

	function convertCodepageStringToUtf8($codepage, $string)
	{	
		$charset = Conversion::getCodepageCharset($codepage);
		
		return iconv($charset, "utf-8", $string);
	}
}

/**
 * Shortcut to Conversion::hex2bin
 * Complementary function to bin2hex() which converts a hex entryid to a binary entryid.
 * Since PHP 5.4 an internal hex2bin() implementation is available.
 * @see Conversion::hex2bin()
 */
if (!function_exists("hex2bin")) {
	function hex2bin($data) {
		return Conversion::hex2bin($data);
	}
}

/**
* Shortcut to Conversion::utf8_to_windows1252
* @see Conversion::utf8_to_windows1252()
*/
function u2w($string) {
	return $string;
}

/**
* Shortcut to Conversion::utf8_to_windows1252
* @see Conversion::utf8_to_windows1252()
* @deprecated Use u2w now
*/
function utf8_to_windows1252($string) {
	return $string;
}

/**
* Shortcut to Conversion::windows1252_to_utf8
* @see Conversion::windows1252_to_utf8()
*/
function w2u($string){
	return $string;
}

/**
* Shortcut to Conversion::windows1252_to_utf8
* @see Conversion::windows1252_to_utf8()
* @deprecated Use w2u now
*/
function windows1252_to_utf8($string){
	return $string;
}

