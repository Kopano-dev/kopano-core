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
function lang_strip_mod($x)
{
	return preg_replace('/@[-A-Za-z\d]+/i', "", $x);
}

function lang_strip_enc($x)
{
	return preg_replace('/\.[-A-Za-z\d]+/i', "", $x);
}

function lang_strip_cc($x)
{
	return preg_replace('/_[A-Z\d]+/i', "", $x);
}

function lang_add_cc($x)
{
	$cc = "";

	if (strpos($x, '_') !== false)
		/* Already has a country code in it */
		return $x;

	$x = lang_strip_cc(lang_strip_enc(lang_strip_mod($x)));
	     if (strcmp($x, "ca") == 0) $cc = "ES";
	else if (strcmp($x, "cs") == 0) $cc = "CZ";
	else if (strcmp($x, "da") == 0) $cc = "DK";
	else if (strcmp($x, "en") == 0) $cc = "US";
	else if (strcmp($x, "et") == 0) $cc = "EE";
	else if (strcmp($x, "he") == 0) $cc = "IL";
	else if (strcmp($x, "ja") == 0) $cc = "JP";
	else if (strcmp($x, "ko") == 0) $cc = "KR";
	else if (strcmp($x, "nb") == 0) $cc = "NO";
	else if (strcmp($x, "nn") == 0) $cc = "NO";
	else if (strcmp($x, "sl") == 0) $cc = "SI";
	else if (strcmp($x, "sv") == 0) $cc = "SE";
	else if (strcmp($x, "uk") == 0) $cc = "UA";
	else {
		if (!preg_match('/^([a-z\d]+)/i', $x, $mat))
			$cc = "wtf";
		else
			$cc = strtoupper($mat[1]);
	}
	return preg_replace('/^([a-z\d]+)/','$1_'.$cc, $x);
}

function lang_add_enc($x)
{
	if (strpos($x, '.') !== false)
		return $x;
	/* If encoding is absent, put UTF-8 in */
	return preg_replace('/^([a-z]+(?:_[A-Z]+)?)(?!\.)/i',
	       '$1.UTF-8', $x);
}

function lang_is_acceptable($locale)
{
	if (strpos($locale, '/') !== false)
		/* Avoid accepting anything that looks like a directory. */
		return false;
	if (strcmp($locale, "last") == 0)
		/*
		 * "last" is an artifact of the caller, the latter of which
		 * does not bother to prefilter it. So we have to weed "last"
		 * out in this function :-(
		 */
		return false;
	/*
	 * All remaining strings are valid locale specifiers. gettext users
	 * expect English rather than a error box (which does not seem to work
	 * reliably anyway). Do not outsmart them.
	 */
	return true;
}

function lang_mo_file_for($locale)
{
	/*
	 * Reimplement the search logic, for the javascript parts.
	 * locale = de_DE.UTF-8
	 */

	if (!lang_is_acceptable($locale)) {
		/* Reject directory separators. */
		return false;
	}

  	$vals = array(
		$locale,
		lang_strip_mod($locale),
		lang_strip_enc($locale),
		lang_strip_mod(lang_strip_enc($locale)),
		lang_strip_cc($locale),
		lang_strip_cc(lang_strip_mod($locale)),
		lang_strip_cc(lang_strip_enc($locale)),
		lang_strip_cc(lang_strip_mod(lang_strip_enc($locale))),
	);
	foreach ($vals as $k) {
		$f = LANGUAGE_DIR."/$k/LC_MESSAGES/kopano_webaccess.mo";
		if (is_file($f))
			return $f;
	}
	return false;
}

	/**
	 * Language handling class
	 *
	 * @package core
	 */
	class Language {
		var $languages;
		var $languagetable;
		var $lang;
		var $loaded;
			
        /**
        * Default constructor
        *
        * By default, the Language class only knows about en (English). If you want more languages, you
        * must call loadLanguages().
        *
        */
		function Language()
		{
			$this->languages = array("en" => "English");
			$this->languagetable = array("en" => "eng");
			$this->loaded = false;
		}
		
		/**
		* Loads languages from disk
		*
		* loadLanguages() reads the languages from disk by reading LANGUAGE_DIR and opening all directories
		* in that directory. Each directory must contain a 'language.txt' file containing:
		*
		* <language display name>
		* <win32 language name>
		*
		* For example:
		* <code>
		* Nederlands
		* nld_NLD
		* </code>
		*
		* Also, the directory names must have a name that is:
		* 1. Available to the server's locale system
		* 2. In the UTF-8 charset
		*
		* For example, nl_NL.UTF-8
		*
		*/
		function loadLanguages()
		{
			if($this->loaded)
				return;

			$languages = explode(";", ENABLED_LANGUAGES);
			$dh = opendir(LANGUAGE_DIR);
			while (($entry = readdir($dh)) !== false) {
				$pick = 0;
				/*
				 * Case 1: languages contains a generalization.
				 * entry = zh_CN, languages = [zh]
				 */
				if (in_array($entry, $languages) ||
				    in_array(lang_strip_mod($entry), $languages) ||
				    in_array(lang_strip_enc($entry), $languages) ||
				    in_array(lang_strip_enc(lang_strip_mod($entry)), $languages) ||
				    in_array(lang_strip_cc($entry), $languages) ||
				    in_array(lang_strip_cc(lang_strip_mod($entry)), $languages) ||
				    in_array(lang_strip_cc(lang_strip_enc($entry)), $languages) ||
				    in_array(lang_strip_cc(lang_strip_enc(lang_strip_mod($entry))), $languages))
					$pick = 1;
				/*
				 * Case 2: language contains a country
				 * specialization that matches the default
				 * country for a general language.
				 * entry = de, language = [de_DE]
				 */
				if (in_array(lang_add_cc($entry), $languages))
					$pick = 1;
				if (!$pick)
					continue;
				if (!is_dir(LANGUAGE_DIR.$entry."/LC_MESSAGES"))
					continue;
				if (!is_file(LANGUAGE_DIR.$entry."/language.txt"))
					continue;
				$fh = fopen(LANGUAGE_DIR.$entry."/language.txt", "r");
				$lang_title = fgets($fh);
				$lang_table = fgets($fh);
				fclose($fh);
				/*
				 * Always give the Preferences dialog valid
				 * _gettext_ IDs (not directory names, ffs),
				 * i.e. entries of the type "lc_CC" or
				 * "lc_CC.UTF-8".
				 */
				$entry = lang_add_enc(lang_add_cc($entry));
				$this->languages[$entry] = $lang_title;
				$this->languagetable[$entry] = $lang_table;
			}
			$this->loaded = true;		
		}

		/**
		* Attempt to set language
		*
		* setLanguage attempts to set the language to the specified language. The language passed
		* is the name of the directory containing the language.
		*
		* For setLanguage() to success, the language has to have been loaded via loadLanguages() AND
		* the gettext system on the system must 'know' the language specified.
		*
		* @param string $lang Language (eg nl_NL.UTF-8)
		*/
		function setLanguage($lang)
		{
			$lang = (empty($lang)||$lang=="C")?LANG:$lang; // default language fix

			/**
			 * for backward compatibility
			 * convert all locale from utf-8 strings to UTF-8 string because locales are case sensitive and
			 * older WA settings can contain utf-8, so we have to convert it to UTF-8 before using
			 * otherwise languiage will be reseted to english
			 */
			if(strpos($lang, "utf-8") !== false) {
				$lang = str_replace("utf-8", "UTF-8", $lang);		// case sensitive replace

				// change it in settings also
				if(strcmp($GLOBALS["settings"]->get("global/language", ""), $lang)) {		// case insensitive compare
					$GLOBALS["settings"]->set("global/language", $lang);
				}
			}

			if (lang_is_acceptable($lang)) {
				$this->lang = $lang;
		
				if (strtoupper(substr(PHP_OS, 0, 3)) === "WIN"){
					$this->loadLanguages(); // we need the languagetable for windows...
					setlocale(LC_MESSAGES,$this->languagetable[$lang]);
					setlocale(LC_TIME,$this->languagetable[$lang]);
				}else{
					setlocale(LC_MESSAGES,$lang);
					setlocale(LC_TIME,$lang);
				}

				bindtextdomain('kopano_webaccess' , LANGUAGE_DIR);

				// All text from gettext() and _() is in UTF-8 so if you're saving to
				// MAPI directly, don't forget to convert to windows-1252 if you're writing
				// to PT_STRING8
				bind_textdomain_codeset('kopano_webaccess', "UTF-8");

				if(isset($GLOBALS['PluginManager'])){
					// What we did above, we are also now going to do for each plugin that has translations.
					$pluginTranslationPaths = $GLOBALS['PluginManager']->getTranslationFilePaths();
					foreach($pluginTranslationPaths as $pluginname => $path){
						bindtextdomain('plugin_'.$pluginname , $path);
						bind_textdomain_codeset('plugin_'.$pluginname, "UTF-8");
					}
				}

				textdomain('kopano_webaccess');
			}else{
				trigger_error("Unknown language: '".$lang."'", E_USER_WARNING);
			}
		}

		/**
		* Return a list of supported languages
		*
		* Returns an associative array in the format langid -> langname, for example "nl_NL.utf8" -> "Nederlands"
		*
		* @return array List of supported languages
		*/
		function getLanguages()
		{
			$this->loadLanguages();
			return $this->languages;
		}
	
		/**
		* Returns the ID of the currently selected language
		*
		* @return string ID of selected language
		*/
		function getSelected()
		{
			return $this->lang;
		}
	
		/**
		* Returns if the specified language is valid or not
		*
		* @param string $lang 
		* @return boolean TRUE if the language is valid
		*/
		function is_language($lang)
		{
			return lang_is_acceptable($lang);
		}

		function getTranslations(){
			$translations = Array();

			$translations['kopano_webaccess'] = $this->getTranslationsFromFile(lang_mo_file_for($this->getSelected()));
			if(!$translations['kopano_webaccess']) $translations['kopano_webaccess'] = Array();

			if(isset($GLOBALS['PluginManager'])){
				// What we did above, we are also now going to do for each plugin that has translations.
				$pluginTranslationPaths = $GLOBALS['PluginManager']->getTranslationFilePaths();
				foreach($pluginTranslationPaths as $pluginname => $path){
					$plugin_translations = $this->getTranslationsFromFile($path.'/'.$this->getSelected().'/LC_MESSAGES/plugin_'.$pluginname.'.mo');
					if($plugin_translations){
						$translations['plugin_'.$pluginname] = $plugin_translations;
					}
				}
			}
			
			return $translations;
		}

		/**
		 * getTranslationsFromFile
		 * 
		 * This file reads the translations from the binary .mo file and returns
		 * them in an array containing the original and the translation variant.
		 * The .mo file format is described on the following URL.
		 * http://www.gnu.org/software/gettext/manual/gettext.html#MO-Files
		 * 
		 *          byte
		 *               +------------------------------------------+
		 *            0  | magic number = 0x950412de                |
		 *               |                                          |
		 *            4  | file format revision = 0                 |
		 *               |                                          |
		 *            8  | number of strings                        |  == N
		 *               |                                          |
		 *           12  | offset of table with original strings    |  == O
		 *               |                                          |
		 *           16  | offset of table with translation strings |  == T
		 *               |                                          |
		 *           20  | size of hashing table                    |  == S
		 *               |                                          |
		 *           24  | offset of hashing table                  |  == H
		 *               |                                          |
		 *               .                                          .
		 *               .    (possibly more entries later)         .
		 *               .                                          .
		 *               |                                          |
		 *            O  | length & offset 0th string  ----------------.
		 *        O + 8  | length & offset 1st string  ------------------.
		 *                ...                                    ...   | |
		 *  O + ((N-1)*8)| length & offset (N-1)th string           |  | |
		 *               |                                          |  | |
		 *            T  | length & offset 0th translation  ---------------.
		 *        T + 8  | length & offset 1st translation  -----------------.
		 *                ...                                    ...   | | | |
		 *  T + ((N-1)*8)| length & offset (N-1)th translation      |  | | | |
		 *               |                                          |  | | | |
		 *            H  | start hash table                         |  | | | |
		 *                ...                                    ...   | | | |
		 *    H + S * 4  | end hash table                           |  | | | |
		 *               |                                          |  | | | |
		 *               | NUL terminated 0th string  <----------------' | | |
		 *               |                                          |    | | |
		 *               | NUL terminated 1st string  <------------------' | |
		 *               |                                          |      | |
		 *                ...                                    ...       | |
		 *               |                                          |      | |
		 *               | NUL terminated 0th translation  <---------------' |
		 *               |                                          |        |
		 *               | NUL terminated 1st translation  <-----------------'
		 *               |                                          |
		 *                ...                                    ...
		 *               |                                          |
		 *               +------------------------------------------+
		 * 
		 * @param $filename string Name of the .mo file.
		 * @return array|boolean false when file is missing otherwise array with
		 *                             translations.
		 */

		function getTranslationsFromFile($filename){
			if(!is_file($filename)) return false;

			$fp = fopen($filename, 'r');
			if(!$fp){return false;}

			// Get number of strings in .mo file
			fseek($fp, 8, SEEK_SET);
			$num_of_str = unpack('Lnum', fread($fp, 4));
			$num_of_str = $num_of_str['num'];

			// Get offset to table with original strings
			fseek($fp, 12, SEEK_SET);
			$offset_orig_tbl = unpack('Loffset', fread($fp, 4));
			$offset_orig_tbl = $offset_orig_tbl['offset'];

			// Get offset to table with translation strings
			fseek($fp, 16, SEEK_SET);
			$offset_transl_tbl = unpack('Loffset', fread($fp, 4));
			$offset_transl_tbl = $offset_transl_tbl['offset'];

			// The following arrays will contain the length and offset of the strings
			$data_orig_strs = Array();
			$data_transl_strs = Array();

			/**
			 * Get the length and offset to the original strings by using the table 
			 * with original strings
			 */
			// Set pointer to start of orig string table
			fseek($fp, $offset_orig_tbl, SEEK_SET);
			for($i=0;$i<$num_of_str;$i++){
				// Length 4 bytes followed by offset 4 bytes
				$length = unpack('Llen', fread($fp, 4));
				$offset = unpack('Loffset', fread($fp, 4));
				$data_orig_strs[$i] = Array( 'length' => $length['len'], 'offset' => $offset['offset'] );
			}

			/**
			 * Get the length and offset to the translation strings by using the table 
			 * with translation strings
			 */
			// Set pointer to start of translations string table
			fseek($fp, $offset_transl_tbl, SEEK_SET);
			for($i=0;$i<$num_of_str;$i++){
				// Length 4 bytes followed by offset 4 bytes
				$length = unpack('Llen', fread($fp, 4));
				$offset = unpack('Loffset', fread($fp, 4));
				$data_transl_strs[$i] = Array( 'length' => $length['len'], 'offset' => $offset['offset'] );
			}

			// This array will contain the actual original and translation strings
			$translation_data = Array();

			// Get the original strings using the length and offset
			for($i=0;$i<count($data_orig_strs);$i++){
				$translation_data[$i] = Array();

				// Set pointer to the offset of the string
				fseek($fp, $data_orig_strs[$i]['offset'], SEEK_SET);
				if($data_orig_strs[$i]['length'] > 0){	// fread does not accept length=0
					$length = $data_orig_strs[$i]['length'];
					$orig_str = unpack('a'.$length.'str', fread($fp, $length));
					$translation_data[$i]['orig'] = $orig_str['str'];	// unpack converts to array :S
				}else{
					$translation_data[$i]['orig'] = '';
				}
			}

			// Get the translation strings using the length and offset
			for($i=0;$i<count($data_transl_strs);$i++){
				// Set pointer to the offset of the string
				fseek($fp, $data_transl_strs[$i]['offset'], SEEK_SET);
				if($data_transl_strs[$i]['length'] > 0){	// fread does not accept length=0
					$length = $data_transl_strs[$i]['length'];
					$trans_str = unpack('a'.$length.'str', fread($fp, $length));
					$translation_data[$i]['trans'] = $trans_str['str'];	// unpack converts to array :S
				}else{
					$translation_data[$i]['trans'] = '';
				}
			}

			return $translation_data;
		}
	}
?>
