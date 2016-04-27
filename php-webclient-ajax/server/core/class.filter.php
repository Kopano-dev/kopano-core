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
	require_once('htmlfilter.php');
	/**
	 * Filters text messages for various uses
	 * 		 	
	 * @package core
	 */
	
	class filter
	{
		/**
		 * Create script-safe HTML from raw HTML
		 *
		 * Note that the function inline_attachments() in util.php is used to convert inline attachments to
		 * the correct HTML to show the message in the browser window.
		 *
		 * @see inline_attachments()
		 *
		 * @param string $html The HTML text
		 * @param string $storeid MAPI binary entryid, is used for the inline attachments and new mail
		 * @param string $entryid MAPI message entryid, is used for the inline attachments and new mail
		 * @return string safe html		
		 */		 		
		function safeHTML($html, $storeid = false, $entryid = false, $attachNum = false)
		{
			// Save all "<" symbols
			$html = preg_replace("/<(?=[^a-zA-Z\/\!\?\%])/", "&lt;", $html); 
			// Opera6 bug workaround
			$html = str_replace("\xC0\xBC", "&lt;", $html);
			
			if(!DISABLE_HTMLBODY_FILTER){
				// Filter '<script>'
				$html = $this->filterScripts($html);
			}
			
			if($storeid && $entryid){
				// Set GLOBALS for preg_replace_callback functions.
				$GLOBALS["preg_replace"] = array();
				$GLOBALS["preg_replace"]["storeid"] = $storeid;
				$GLOBALS["preg_replace"]["entryid"] = $entryid;
			}

			// Set GLOBALS for preg_replace_callback functions.
			$GLOBALS["preg_replace"] = array();
			$GLOBALS["preg_replace"]["storeid"] = $storeid;
			$GLOBALS["preg_replace"]["entryid"] = $entryid;
			// When inline_img_attachments() tries to use attachNum in the function join it needs to
			// be of the Array type and cannot be false. We do need to support false to be passed as
			// an argument though, as other components can have their attachNum set to false.
			if(is_array($attachNum)){
				$GLOBALS["preg_replace"]["attachNum"] = $attachNum;
			}else{
				$GLOBALS["preg_replace"]["attachNum"] = Array();
			}

			// inline images can be specified without cid: tag also so replace that also, but exclude external link to images
			$html = preg_replace_callback('/<img[^>]*src\s*=\s*[\"\']([^\"\']*)[\"\'].*?>/msi', "inline_img_attachments", $html);

			// all image tags will be processed by above line, still cid: can be specified with css also
			// so this line will handle cid: in all tags
			$html = preg_replace_callback("/=[\"']?(cid:)([^\"'>]+)[\"']?/msi", "inline_attachments", $html);

			// Replace all 'mailto:..' with link to compose new mail
			$html = preg_replace_callback('/<(a[^>]*)(href)=(["\'])?mailto:([^"\'>\?]+)\??([^"\'>]*)(["\']?)([^>]*)>/msi','mailto_newmail',$html);
			
			// remove 'base target' if exists
			$html = preg_replace("/<base[^>]*target=[^>]*\/?>/msi",'',$html);
			
			// Add 'base target' after the head-tag
			$base = '<base target="_blank">';
			$html = preg_replace("/<(head[^>]*)>/msi",('<$1>'.$base),$html);

			// if no head-tag was found (no base target is then added), add the 'base target' above the file
			if(strpos($html, $base)===false){
				$html = $base . $html;
			}

			// add default font
			$font = '<style type="text/css">body { font-family: monospace; }</style>';
			$html = preg_replace("/<(head[^>]*)>/msi",('<$1>'.$font),$html);

			// if no head-tag was found (no default font is then added), add the 'font default' above the file
			if(strpos($html, $font)===false){
				$html = $font . $html;
			}

			return $html;
		} 
		
		/**
		 * Filter scripts from HTML
		 *
		 * @access private
		 * @param string $str string which should be filtered
		 * @return string string without any script tags		
		 */		 		
		function filterScripts($str)
		{
			/**
			 * Here we perform two regular expressions to find the propper HREF attribute. We are doing two because the
			 * value of an HTML attribute does not need to be in quotes. First we perform the quoted regex.
			 * <base                    Start with the base tag
			 * [^>]*                    The we have any character until the next statement which looks for a href 
			 *                           attribute
			 *  href=                   Now we continue with an href attribute preceeded by a space(!)
			 * [\'\"]([^\'\">]*)[\'\"]  This part looks for a string inside the quoted value of the attribute. It 
			 *                           searches for all characters except a quote or a tag-closing character. The 
			 *                           latter is to prevent having an issue when dealing with dirty HTML (forgotten 
			 *                           quotes).
			 * [^>]*>                   End with any character until the end of the HTML tag
			 * 
			 * Now we continue with the non-quoted regex
			 * <base      Same as above
			 * [^>]*      Same as above
			 *  href=     Same as above, again with space(!) preceeding the attribute name
			 * ([^ >]*)   This part looks for any character right after the "href=" that is not a space or an tag-closing
			 *             character. Again the latter is done too prevent getting weird results with dirty HTML.
			 * [^>]*>     Same as above
			 * 
			 * If the quoted regex does not turn up any results we will use the non-quoted one. If there are multiple 
			 * href attributes in the tag then the regex will take the last one. When looking at the following HTML 
			 * <base href="test1" href=test2> the first one (11) will be used because the quoted regex is looked at 
			 * first.
			 */ 
			$baseMatchQuoted    = preg_match('/<base[^>]* href=[\'\"]([^\'\">]*)[\'\"][^>]*>/msi',$str, $matchQuoted);
			$baseMatchNonQuoted = preg_match('/<base[^>]* href=([^ >]*)[^>]*>/msi',$str, $matchNonQuoted);
			$baseHref = false;
			if($baseMatchQuoted === 1){
				$baseHref = $matchQuoted[1];
			}elseif($baseMatchNonQuoted === 1){
				$baseHref = $baseMatchNonQuoted[1];
			}

			$str = magicHTML($str, 0);

			// Check the url in the b$baseHref and see if it starts with a protocol to prevent JS injection.
			if($baseHref && preg_match('/^[a-zA-Z0-9]+:\/\//', $baseHref) === 1){
				$base = '<base href="'.$baseHref.'" />';
				// Add 'base href' after the head-tag
				$str = preg_replace("/<(head[^>]*)>/msi",('<$1>'.$base),$str);

				// if no head-tag was found (no base href is then added), add the 'base href' above the file
				if(strpos($str, $base)===false){
					$str = $base . $str;
				}
			}
			return $str;
		} 
	
		/**
		 * Convert HTML to text
		 *
		 * @param string $str the html which should be converted to text
		 * @return string plain text version of the given $str				
		 */		 		
		function html2text($str)
		{
			return $this->unhtmlentities(preg_replace(
					Array("'<(HEAD|SCRIPT|STYLE|TITLE)[^>]*?>.*?</(HEAD|SCRIPT|STYLE|TITLE)[^>]*?>'si",
						"'(\r|\n)'",
						"'<BR[^>]*?>'i",
						"'<P[^>]*?>'i",
						"'<\/?\w+[^>]*>'e",
						"'<![^>]*>'s"
						),
					Array("",
						"",
						"\r\n",
						"\r\n\r\n",
						"",
						""),
					$str));
		} 
		
		/**
		 * Remove HTML entities and convert them to single characters where possible
		 *
		 * @access private
		 * @param string $str string which should be converted
		 * @return string converted string						
		 */
		function unhtmlentities ($string)
		{
			$trans_tbl = get_html_translation_table(HTML_ENTITIES);
			$trans_tbl = array_flip($trans_tbl);
			return strtr($string, $trans_tbl);
		} 
	
		/**
		 * Remove script tags from HTML source
		 *
		 * @access private
		 * @param string $str the html which the events should be filtered
		 * @return string html with no 'on' events				
		 */
		function _filter_tag($str)
		{
			// fix $str when called by preg_replace_callback
			if (is_array($str)) $str = $str[0];
			
			// fix unicode
			$str = preg_replace_callback("|(%[0-9A-Z]{2})|i", create_function('$str', 'return chr(hexdec($str[0]));'), $str);

			$matches = Array(
				// (\bON\w+(?!.)) - matches string beginning with 'on' only if the string is not followed by '.'	for example 'online.nl' will not be matched, whereas 'onmouse' will be.
				"'(\bON\w+(?!.))'i", // events
				"'(HREF)( *= *[\"\']?\w+SCRIPT *:[^\"\' >]+)'i", // links
				"'\n'",
				"'\r'"
				);
			$replaces = Array(
				"\\1_filtered",
				"\\1_filtered\\2",
				" ",
				" ",
				);
			return preg_replace($matches, $replaces, $str);
		} 
	} 
?>
