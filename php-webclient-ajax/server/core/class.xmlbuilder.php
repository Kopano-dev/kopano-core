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
	* XML serialiser class
	*
	* This class builds an XML string. It receives an associative array, which 
	* will be converted to a XML string. The data in the associative array is
	* assumed to hold UTF-8 strings, and the output XML is output as UTF-8 also;
	* no charset processing is performed.
	*
	* @package core
	*/
	
	class XMLBuilder
	{
		/**
		 * @var string this string the builded XML
		 */	
		var $xml;
		
		/**
		 * @var integer this variable is used for indenting (DEBUG)
		 */ 		 		
		var $depth;
		
		function XMLBuilder()
		{
			if(defined("DEBUG_XML_INDENT") && DEBUG_XML_INDENT) {
			    $this->indent = true;
            } else {
                $this->indent = false;
            }
		} 

		/**
		 * Builds the XML using the given associative array
		 * @param array $data The data which should be converted to a XML string
		 * @return string The serialised XML string
		 */		 		
		function build($data)
		{
			$this->depth = 1;
			$this->xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?" . ">\n<zarafa>\n";
			$this->addData($data);
			$this->xml .= "</zarafa>";
			return $this->xml;
		}

		/**
		 * Add data to serialised XML string
		 *
		 * The type of data is autodetected and forwarded to the correct add*() function
		 *
		 * @access private
		 * @param array $data array of data which should be converted to a XML string		 
		 */ 		 				
		function addData($data)
		{
			if(is_array($data)) {
				foreach($data as $tagName => $tagValue)
				{
					if(is_assoc_array($tagValue)) {
						$this->addAssocArray($tagName, $tagValue);
					} else if(is_array($tagValue)) {
						$this->addArray($tagName, $tagValue);
					} else {
						$this->addNode($tagName, $tagValue);
					}
				}
			}
		}

		/**
		 * Adds a non-associated array to the XML string.
		 * @access private
		 * @param string $parentTag the parent tag of all items in the array
		 * @param array $data array of data
		 */ 		 				
		function addArray($parentTag, $data)
		{
			foreach($data as $tagValue)
			{
				$this->addAssocArray($parentTag, $tagValue);
			}
		}

		/**
		 * Adds an associative array to the XML string.
		 * @access private
		 * @param string $parentTag the parent tag		 
		 * @param array $data array of data 		 
		 */ 		 				
		function addAssocArray($parentTag, $data)
		{
			$attributes = $this->getAttributes($data);
			if($this->indent)
    			$this->xml .= str_repeat("\t",$this->depth);
			$this->xml .=  "<" . $parentTag . $attributes . ">";

			if(is_array($data)) {

				if(isset($data["_content"])) {
					if (!is_array($data["_content"])){
						$this->xml .= xmlentities($data["_content"]);
					}else{
						$this->depth++; 
						if($this->indent)
    						$this->xml .= "\n";
						$this->addData($data["_content"]);
						$this->depth--;
						if($this->indent)
    						$this->xml .= str_repeat("\t",$this->depth);
					}
				} else {
					$this->depth++; 
                    if($this->indent)
    					$this->xml .= "\n";
					$this->addData($data);
					$this->depth--;
					if($this->indent)
						$this->xml .= str_repeat("\t",$this->depth);
				}
			} else {
				$this->xml .= xmlentities($data);
			}
				
			$this->xml .= "</" . $parentTag . ">";
			if($this->indent)
			    $this->xml .= "\n";
		}
		
		/**
		 * Add a node to the XML string.
		 * @access private
		 * @param string $tagName the tag		 
		 * @param string $value the value of the tag 		 
		 */
		function addNode($tagName, $value)
		{
		    if($this->indent)
    			$this->xml .= str_repeat("\t",$this->depth);

			/**
			 * When converting a float value into a string PHP replaces the decimal point into a 
			 * comma if the language settings are set to a language that uses the comma as a decimal
			 * separator. This check makes sure only a decimal point is used in the XML.
			 */
			if(is_float($value)){
				$value = str_replace(',','.',strval($value));
			}

			$this->xml .= "<" . $tagName . ">" . xmlentities($value) . "</" . $tagName . ">";
			if($this->indent)
			    $this->xml .= "\n";
		}
		
		/**
		 * Verify if there are any attributes in the given array.
		 * It returns a string with the attributes, which will be added to the
		 * XML string.
		 * @access private
		 * @param array $data the array which be checked
		 * @return string a string with the attributes		  		 		 		 
		 */		 		
		function getAttributes(&$data)
		{
			$attributes = "";
			
			if(isset($data["attributes"]) && is_array($data["attributes"])) {
				$attributes = "";
				
				foreach($data["attributes"] as $attribute => $value)
				{
					$attributes .= " ".$attribute . "=\"" . xmlentities($value) . "\"";
				}
				
				unset($data["attributes"]);
			}
			
			return $attributes;
		}
	}
?>
