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
 ** HTMLFILTER ROUTINES
 *
 * @copyright 1999-2010 The SquirrelMail Project Team
 * @license http://opensource.org/licenses/gpl-license.php GNU Public License
 * @version $Id: mime.php 13899 2010-01-30 16:14:53Z pdontthink $
 *
 * The sq_fix_urls() function has been modified so that it passes through URLs directly;
 * the output from magicHTML is therefore only an XSS-safe version of the same input.
 */

/**
 * This function checks attribute values for entity-encoded values
 * and returns them translated into 8-bit strings so we can run
 * checks on them.
 *
 * @param  $attvalue A string to run entity check against.
 * @return           Nothing, modifies a reference value.
 */
function sq_defang(&$attvalue){
    $me = 'sq_defang';
    /**
     * Skip this if there aren't ampersands or backslashes.
     */
    if (strpos($attvalue, '&') === false
        && strpos($attvalue, '\\') === false){
        return;
    }
    $m = false;
    do {
        $m = false;
        $m = $m || sq_deent($attvalue, '/\&#0*(\d+);*/s');
        $m = $m || sq_deent($attvalue, '/\&#x0*((\d|[a-f])+);*/si', true);
        $m = $m || sq_deent($attvalue, '/\\\\(\d+)/s', true);
    } while ($m == true);
    $attvalue = stripslashes($attvalue);
}

/**
 * Kill any tabs, newlines, or carriage returns. Our friends the
 * makers of the browser with 95% market value decided that it'd
 * be funny to make "java[tab]script" be just as good as "javascript".
 *
 * @param  attvalue  The attribute value before extraneous spaces removed.
 * @return attvalue  Nothing, modifies a reference value.
 */
function sq_unspace(&$attvalue){
    $me = 'sq_unspace';
    if (strcspn($attvalue, "\t\r\n\0 ") != strlen($attvalue)){
        $attvalue = str_replace(Array("\t", "\r", "\n", "\0", " "),
                                Array('',   '',   '',   '',   ''), $attvalue);
    }
}

/**
 * Translate all dangerous Unicode or Shift_JIS characters which are accepted by
 * IE as regular characters.
 *
 * @param  attvalue  The attribute value before dangerous characters are translated.
 * @return attvalue  Nothing, modifies a reference value.
 * @author Marc Groot Koerkamp.
 */
// IE has the evil habit of accepting every possible value for the attribute expression.
// The table below contains characters which are parsed by IE if they are used in the "expression"
// attribute value.
$aDangerousCharsReplacementTable = array(
     array('&#x029F;', '&#0671;' ,/* L UNICODE IPA Extension */
           '&#x0280;', '&#0640;' ,/* R UNICODE IPA Extension */
           '&#x0274;', '&#0628;' ,/* N UNICODE IPA Extension */
           '&#xFF25;', '&#65317;' ,/* Unicode FULLWIDTH LATIN CAPITAL LETTER E */
           '&#xFF45;', '&#65349;' ,/* Unicode FULLWIDTH LATIN SMALL LETTER E */
           '&#xFF38;', '&#65336;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER X */
           '&#xFF58;', '&#65368;',/* Unicode FULLWIDTH LATIN SMALL LETTER X */
           '&#xFF30;', '&#65328;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER P */
           '&#xFF50;', '&#65360;',/* Unicode FULLWIDTH LATIN SMALL LETTER P */
           '&#xFF32;', '&#65330;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER R */
           '&#xFF52;', '&#65362;',/* Unicode FULLWIDTH LATIN SMALL LETTER R */
           '&#xFF33;', '&#65331;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER S */
           '&#xFF53;', '&#65363;',/* Unicode FULLWIDTH LATIN SMALL LETTER S */
           '&#xFF29;', '&#65321;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER I */
           '&#xFF49;', '&#65353;',/* Unicode FULLWIDTH LATIN SMALL LETTER I */
           '&#xFF2F;', '&#65327;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER O */
           '&#xFF4F;', '&#65359;',/* Unicode FULLWIDTH LATIN SMALL LETTER O */
           '&#xFF2E;', '&#65326;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER N */
           '&#xFF4E;', '&#65358;',/* Unicode FULLWIDTH LATIN SMALL LETTER N */
           '&#xFF2C;', '&#65324;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER L */
           '&#xFF4C;', '&#65356;',/* Unicode FULLWIDTH LATIN SMALL LETTER L */
           '&#xFF35;', '&#65333;',/* Unicode FULLWIDTH LATIN CAPITAL LETTER U */
           '&#xFF55;', '&#65365;',/* Unicode FULLWIDTH LATIN SMALL LETTER U */
           '&#x207F;', '&#8319;' ,/* Unicode SUPERSCRIPT LATIN SMALL LETTER N */
           "\xEF\xBC\xA5", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER E */   // in unicode this is some Chinese char range
           "\xEF\xBD\x85", /* Shift JIS FULLWIDTH LATIN SMALL LETTER E */
           "\xEF\xBC\xB8", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER X */
           "\xEF\xBD\x98", /* Shift JIS FULLWIDTH LATIN SMALL LETTER X */
           "\xEF\xBC\xB0", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER P */
           "\xEF\xBD\x90", /* Shift JIS FULLWIDTH LATIN SMALL LETTER P */
           "\xEF\xBC\xB2", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER R */
           "\xEF\xBD\x92", /* Shift JIS FULLWIDTH LATIN SMALL LETTER R */
           "\xEF\xBC\xB3", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER S */
           "\xEF\xBD\x93", /* Shift JIS FULLWIDTH LATIN SMALL LETTER S */
           "\xEF\xBC\xA9", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER I */
           "\xEF\xBD\x89", /* Shift JIS FULLWIDTH LATIN SMALL LETTER I */
           "\xEF\xBC\xAF", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER O */
           "\xEF\xBD\x8F", /* Shift JIS FULLWIDTH LATIN SMALL LETTER O */
           "\xEF\xBC\xAE", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER N */
           "\xEF\xBD\x8E", /* Shift JIS FULLWIDTH LATIN SMALL LETTER N */
           "\xEF\xBC\xAC", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER L */
           "\xEF\xBD\x8C", /* Shift JIS FULLWIDTH LATIN SMALL LETTER L */
           "\xEF\xBC\xB5", /* Shift JIS FULLWIDTH LATIN CAPITAL LETTER U */
           "\xEF\xBD\x95", /* Shift JIS FULLWIDTH LATIN SMALL LETTER U */
           "\xE2\x81\xBF", /* Shift JIS FULLWIDTH SUPERSCRIPT N */
           "\xCA\x9F", /* L UNICODE IPA Extension */
           "\xCA\x80", /* R UNICODE IPA Extension */
           "\xC9\xB4"),  /* N UNICODE IPA Extension */
    array('l', 'l', 'r','r','n','n',
          'E','E','e','e','X','X','x','x','P','P','p','p','R','R','r','r','S','S','s','s','I','I',
          'i','i','O','O','o','o','N','N','n','n','L','L','l','l','U','U','u','u','n','n',
          'E','e','X','x','P','p','R','r','S','s','I','i','O','o','N','n','L','l','U','u','n','l','r','n'));

function sq_fixIE_idiocy(&$attvalue) {
    global $aDangerousCharsReplacementTable;

    // Shortcut if the value contains only 'normal' characters    
    if(preg_match("/^[a-zA-Z0-9\";:.,\\s-]*\$/s", $attvalue)) {
        return;
    }
    
    // remove NUL
    $attvalue = str_replace("\0", "", $attvalue);
    // remove comments
    $attvalue = preg_replace("/(\/\*.*?\*\/)/","",$attvalue);

    $attvalue = str_replace($aDangerousCharsReplacementTable[0],$aDangerousCharsReplacementTable[1],$attvalue);
    // Escapes are useful for special characters like "{}[]()'&. In other cases they are
    // used for XSS.
    $attvalue = preg_replace("/(\\\\)([a-zA-Z]{1})/",'$2',$attvalue);
}

/**
 * This function returns the final tag out of the tag name, an array
 * of attributes, and the type of the tag. This function is called by
 * sq_sanitize internally.
 *
 * @param  $tagname  the name of the tag.
 * @param  $attary   the array of attributes and their values
 * @param  $tagtype  The type of the tag (see in comments).
 * @return           a string with the final tag representation.
 */
function sq_tagprint($tagname, $attary, $tagtype){
    $me = 'sq_tagprint';

    if ($tagtype == 2){
        $fulltag = '</' . $tagname . '>';
    } else {
        $fulltag = '<' . $tagname;
        if (is_array($attary) && sizeof($attary)){
            $atts = Array();
            while (list($attname, $attvalue) = each($attary)){
                array_push($atts, "$attname=$attvalue");
            }
            $fulltag .= ' ' . join(" ", $atts);
        }
        if ($tagtype == 3){
            $fulltag .= ' /';
        }
        $fulltag .= '>';
    }
    return $fulltag;
}

/**
 * A small helper function to use with array_walk. Modifies a by-ref
 * value and makes it lowercase.
 *
 * @param  $val a value passed by-ref.
 * @return      void since it modifies a by-ref value.
 */
function sq_casenormalize(&$val){
    $val = strtolower($val);
}

/**
 * This function skips any whitespace from the current position within
 * a string and to the next non-whitespace value.
 *
 * @param  $body   the string
 * @param  $offset the offset within the string where we should start
 *                 looking for the next non-whitespace character.
 * @return         the location within the $body where the next
 *                 non-whitespace char is located.
 */
function sq_skipspace($body, $offset){
    $me = 'sq_skipspace';
    while(1) {
        $matches = Array();
        preg_match('/^(\s*)/s', substr($body, $offset, 256), $matches);
        if (sizeof($matches{1})){
            $count = strlen($matches{1});
            if ($count == 0)
                break;
            $offset += $count;
        } else break;
    }
    return $offset;
}

/**
 * This function looks for the next character within a string.  It's
 * really just a glorified "strpos", except it catches if failures
 * nicely.
 *
 * @param  $body   The string to look for needle in.
 * @param  $offset Start looking from this position.
 * @param  $needle The character/string to look for.
 * @return         location of the next occurance of the needle, or
 *                 strlen($body) if needle wasn't found.
 */
function sq_findnxstr($body, $offset, $needle){
    $me  = 'sq_findnxstr';
    $pos = strpos($body, $needle, $offset);
    if ($pos === FALSE){
        $pos = strlen($body);
    }
    return $pos;
}

/**
 * This function takes a PCRE-style regexp and tries to match it
 * within the string.
 *
 * @param  $body   The string to look for needle in.
 * @param  $offset Start looking from here.
 * @param  $reg    A PCRE-style regex to match.
 * @return         Returns a false if no matches found, or an array
 *                 with the following members:
 *                 - integer with the location of the match within $body
 *                 - string with whatever content between offset and the match
 *                 - string with whatever it is we matched
 */
function sq_findnxreg($body, $offset, $reg){
    $me = 'sq_findnxreg';
    $matches = Array();
    $retarr = Array();
    preg_match("%(.*?)($reg)%si", $body, $matches, 0, $offset);
    if (!isset($matches{0}) || !$matches{0}){
        $retarr = false;
    } else {
        $retarr{0} = $offset + strlen($matches{1});
        $retarr{1} = $matches{1};
        $retarr{2} = $matches{2};
    }
    return $retarr;
}

/**
 * This function looks for the next tag.
 *
 * @param  $body   String where to look for the next tag.
 * @param  $offset Start looking from here.
 * @return         false if no more tags exist in the body, or
 *                 an array with the following members:
 *                 - string with the name of the tag
 *                 - array with attributes and their values
 *                 - integer with tag type (1, 2, or 3)
 *                 - integer where the tag starts (starting "<")
 *                 - integer where the tag ends (ending ">")
 *                 first three members will be false, if the tag is invalid.
 */
function sq_getnxtag($body, $offset){
    $me = 'sq_getnxtag';
    if ($offset > strlen($body)){
        return false;
    }
    $lt = sq_findnxstr($body, $offset, "<");
    if ($lt == strlen($body)){
        return false;
    }
    /**
     * We are here:
     * blah blah <tag attribute="value">
     * \---------^
     */
    $pos = sq_skipspace($body, $lt+1);
    if ($pos >= strlen($body)){
        return Array(false, false, false, $lt, strlen($body));
    }
    /**
     * There are 3 kinds of tags:
     * 1. Opening tag, e.g.:
     *    <a href="blah">
     * 2. Closing tag, e.g.:
     *    </a>
     * 3. XHTML-style content-less tag, e.g.:
     *    <img src="blah" />
     */
    $tagtype = false;
    switch ($body[$pos]){
        case '/':
            $tagtype = 2;
            $pos++;
            break;
        case '!':
            /**
             * A comment or an SGML declaration.
             */
            if (substr($body, $pos+1, 2) == "--"){
                $gt = strpos($body, "-->", $pos);
                if ($gt === false){
                    $gt = strlen($body);
                } else {
                    $gt += 2;
                }
                return Array(false, false, false, $lt, $gt);
            } else {
                $gt = sq_findnxstr($body, $pos, ">");
                return Array(false, false, false, $lt, $gt);
            }
            break;
        default:
            /**
             * Assume tagtype 1 for now. If it's type 3, we'll switch values
             * later.
             */
            $tagtype = 1;
            break;
    }

    $tag_start = $pos;
    $tagname = '';
    /**
     * Look for next [\W-_], which will indicate the end of the tag name.
     */
    $regary = sq_findnxreg($body, $pos, "[^\w\-_]");
    if ($regary == false){
        return Array(false, false, false, $lt, strlen($body));
    }
    list($pos, $tagname, $match) = $regary;
    $tagname = strtolower($tagname);

    /**
     * $match can be either of these:
     * '>'  indicating the end of the tag entirely.
     * '\s' indicating the end of the tag name.
     * '/'  indicating that this is type-3 xhtml tag.
     *
     * Whatever else we find there indicates an invalid tag.
     */
    switch ($match){
        case '/':
            /**
             * This is an xhtml-style tag with a closing / at the
             * end, like so: <img src="blah" />. Check if it's followed
             * by the closing bracket. If not, then this tag is invalid
             */
            if (substr($body, $pos, 2) == "/>"){
                $pos++;
                $tagtype = 3;
            } else {
                $gt = sq_findnxstr($body, $pos, ">");
                $retary = Array(false, false, false, $lt, $gt);
                return $retary;
            }
        case '>':
            return Array($tagname, false, $tagtype, $lt, $pos);
            break;
        default:
            /**
             * Check if it's whitespace
             */
            if (!preg_match('/\s/', $match)){
                /**
                 * This is an invalid tag! Look for the next closing ">".
                 */
                $gt = sq_findnxstr($body, $lt, ">");
                return Array(false, false, false, $lt, $gt);
            }
            break;
    }

    /**
     * At this point we're here:
     * <tagname  attribute='blah'>
     * \-------^
     *
     * At this point we loop in order to find all attributes.
     */
    $attname = '';
    $atttype = false;
    $attary = Array();

    while ($pos <= strlen($body)){
        $pos = sq_skipspace($body, $pos);
        if ($pos == strlen($body)){
            /**
             * Non-closed tag.
             */
            return Array(false, false, false, $lt, $pos);
        }
        /**
         * See if we arrived at a ">" or "/>", which means that we reached
         * the end of the tag.
         */
        $matches = Array();
        if (preg_match("%^(>|/>)%s", substr($body, $pos, 2), $matches)) {
            /**
             * Yep. So we did.
             */
            if ($matches{1} == "/>"){
                $tagtype = 3;
                $pos++;
            }
            return Array($tagname, $attary, $tagtype, $lt, $pos);
        }

        /**
         * There are several types of attributes, with optional
         * [:space:] between members.
         * Type 1:
         *   attrname[:space:]=[:space:]'CDATA'
         * Type 2:
         *   attrname[:space:]=[:space:]"CDATA"
         * Type 3:
         *   attr[:space:]=[:space:]CDATA
         * Type 4:
         *   attrname
         *
         * We leave types 1 and 2 the same, type 3 we check for
         * '"' and convert to "&quot" if needed, then wrap in
         * double quotes. Type 4 we convert into:
         * attrname="yes".
         */
        $regary = sq_findnxreg($body, $pos, "[^:\w\-_]");
        if ($regary == false){
            /**
             * Looks like body ended before the end of tag.
             */
            return Array(false, false, false, $lt, strlen($body));
        }
        list($pos, $attname, $match) = $regary;
        $attname = strtolower($attname);
        /**
         * We arrived at the end of attribute name. Several things possible
         * here:
         * '>'  means the end of the tag and this is attribute type 4
         * '/'  if followed by '>' means the same thing as above
         * '\s' means a lot of things -- look what it's followed by.
         *      anything else means the attribute is invalid.
         */
        switch($match){
            case '/':
                /**
                 * This is an xhtml-style tag with a closing / at the
                 * end, like so: <img src="blah" />. Check if it's followed
                 * by the closing bracket. If not, then this tag is invalid
                 */
                if (substr($body, $pos, 2) == "/>"){
                    $pos++;
                    $tagtype = 3;
                } else {
                    $gt = sq_findnxstr($body, $pos, ">");
                    $retary = Array(false, false, false, $lt, $gt);
                    return $retary;
                }
            case '>':
                $attary{$attname} = '"yes"';
                return Array($tagname, $attary, $tagtype, $lt, $pos);
                break;
            default:
                /**
                 * Skip whitespace and see what we arrive at.
                 */
                $pos = sq_skipspace($body, $pos);
                $char = substr($body, $pos, 1);
                /**
                 * Two things are valid here:
                 * '=' means this is attribute type 1 2 or 3.
                 * \w means this was attribute type 4.
                 * anything else we ignore and re-loop. End of tag and
                 * invalid stuff will be caught by our checks at the beginning
                 * of the loop.
                 */
                if ($char == "="){
                    $pos++;
                    $pos = sq_skipspace($body, $pos);
                    /**
                     * Here are 3 possibilities:
                     * "'"  attribute type 1
                     * '"'  attribute type 2
                     * everything else is the content of tag type 3
                     */
                    $quot = substr($body, $pos, 1);
                    if ($quot == "'"){
                        $regary = sq_findnxreg($body, $pos+1, "\'");
                        if ($regary == false){
                            return Array(false, false, false, $lt, strlen($body));
                        }
                        list($pos, $attval, $match) = $regary;
                        $pos++;
                        $attary{$attname} = "'" . $attval . "'";
                    } else if ($quot == '"'){
                        $regary = sq_findnxreg($body, $pos+1, '\"');
                        if ($regary == false){
                            return Array(false, false, false, $lt, strlen($body));
                        }
                        list($pos, $attval, $match) = $regary;
                        $pos++;
                        $attary{$attname} = '"' . $attval . '"';
                    } else {
                        /**
                         * These are hateful. Look for \s, or >.
                         */
                        $regary = sq_findnxreg($body, $pos, "[\s>]");
                        if ($regary == false){
                            return Array(false, false, false, $lt, strlen($body));
                        }
                        list($pos, $attval, $match) = $regary;
                        /**
                         * If it's ">" it will be caught at the top.
                         */
                        $attval = preg_replace("/\"/s", "&quot;", $attval);
                        $attary{$attname} = '"' . $attval . '"';
                    }
                } else if (preg_match("|[\w/>]|", $char)) {
                    /**
                     * That was attribute type 4.
                     */
                    $attary{$attname} = '"yes"';
                } else {
                    /**
                     * An illegal character. Find next '>' and return.
                     */
                    $gt = sq_findnxstr($body, $pos, ">");
                    return Array(false, false, false, $lt, $gt);
                }
                break;
        }
    }
    /**
     * The fact that we got here indicates that the tag end was never
     * found. Return invalid tag indication so it gets stripped.
     */
    return Array(false, false, false, $lt, strlen($body));
}

/**
 * Translates entities into literal values so they can be checked.
 *
 * @param $attvalue the by-ref value to check.
 * @param $regex    the regular expression to check against.
 * @param $hex      whether the entites are hexadecimal.
 * @return          True or False depending on whether there were matches.
 */
function sq_deent(&$attvalue, $regex, $hex=false){
    $me = 'sq_deent';
    $ret_match = false;
    preg_match_all($regex, $attvalue, $matches);
    if (is_array($matches) && sizeof($matches[0]) > 0){
        $repl = Array();
        for ($i = 0; $i < sizeof($matches[0]); $i++){
            $numval = $matches[1][$i];
            if ($hex){
                $numval = hexdec($numval);
            }
            if($numval < 128)
                $repl{$matches[0][$i]} = chr($numval);
        }
        if(count($repl) > 0) {
            $attvalue = strtr($attvalue, $repl);
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

/**
 * This function runs various checks against the attributes.
 *
 * @param  $tagname         String with the name of the tag.
 * @param  $attary          Array with all tag attributes.
 * @param  $rm_attnames     See description for sq_sanitize
 * @param  $bad_attvals     See description for sq_sanitize
 * @param  $add_attr_to_tag See description for sq_sanitize
 * @param  $message         message object
 * @param  $id              message id
 * @param  $mailbox         mailbox
 * @return                  Array with modified attributes.
 */
function sq_fixatts($tagname,
                    $attary,
                    $rm_attnames,
                    $bad_attvals,
                    $add_attr_to_tag,
                    $message,
                    $id,
                    $mailbox
                    ){
    $me = 'sq_fixatts';
    while (list($attname, $attvalue) = each($attary)){
        /**
         * See if this attribute should be removed.
         */
        foreach ($rm_attnames as $matchtag=>$matchattrs){
            if (preg_match($matchtag, $tagname)){
                foreach ($matchattrs as $matchattr){
                    if (preg_match($matchattr, $attname)){
                        unset($attary{$attname});
                        continue;
                    }
                }
            }
        }

        if ($attname == 'href' || $attname == 'src' || $attname == 'background') {
            # If you type \\server\share into Outlook, then it will put
            # file:/// in front of it, making it file:///\\server\share.
            # Transform this into file://///server/share.  The extra
            # slashes are required to make it work for Firefox as well.
            if (preg_match("/^['\"]file:\\/\\/\\/?\\\\\\\\([^\\\\]+)\\\\([^\\\\]+)(\\\\.*)?['\"]$/", $attvalue, $aMatch)) {
                $attvalue = "\"file://///" . $aMatch[1] . "/" .
                    $aMatch[2] . str_replace(Array("\\"), Array("/"),
                                             $aMatch[3]) . "\"";
            }
        }

        /**
         * Workaround for IE quirks
         */
        sq_fixIE_idiocy($attvalue);

        /**
         * Remove any backslashes, entities, and extraneous whitespace.
         */
        $oldattvalue = $attvalue;
        sq_defang($attvalue);
        if ($attname == 'style' && $attvalue !== $oldattvalue) {
            // entities are used in the attribute value. In 99% of the cases it's there as XSS
            // i.e.<div style="{ left:exp&#x0280;essio&#x0274;( alert('XSS') ) }">
            $attvalue = "idiocy";
            $attary{$attname} = $attvalue;
        }
        sq_unspace($attvalue);

        /**
         * Now let's run checks on the attvalues.
         * I don't expect anyone to comprehend this. If you do,
         * get in touch with me so I can drive to where you live and
         * shake your hand personally. :)
         */
        foreach ($bad_attvals as $matchtag=>$matchattrs){
            if (preg_match($matchtag, $tagname)){
                foreach ($matchattrs as $matchattr=>$valary){
                    if (preg_match($matchattr, $attname)){
                        /**
                         * There are two arrays in valary.
                         * First is matches.
                         * Second one is replacements
                         */
                        list($valmatch, $valrepl) = $valary;
                        $newvalue =
                            preg_replace($valmatch, $valrepl, $attvalue);
                        if ($newvalue != $attvalue){
                            $attary{$attname} = $newvalue;
                            $attvalue = $newvalue;
                        }
                    }
                }
            }
        }

        if ($attname == 'style') {
            if (preg_match('/[\0-\37\200-\377]+/',$attvalue)) {
                // 8bit and control characters in style attribute values can be used for XSS, remove them
                $attary{$attname} = '"disallowed character"';
            }
            preg_match_all("/url\s*\((.+)\)/si",$attvalue,$aMatch);
            if (count($aMatch)) {
                foreach($aMatch[1] as $sMatch) {
                    // url value
                    $urlvalue = $sMatch;
                    sq_fix_url($attname, $urlvalue, $message, $id, $mailbox,"'");
                    $attary{$attname} = str_replace($sMatch,$urlvalue,$attvalue);
                }
            }
        }
        /**
         * Use white list based filtering on attributes which can contain url's
         */
        else if ($attname == 'href' || $attname == 'src' || $attname == 'background') {
            sq_fix_url($attname, $attvalue, $message, $id, $mailbox);
            $attary{$attname} = $attvalue;
        }
    }
    /**
     * See if we need to append any attributes to this tag.
     */
    foreach ($add_attr_to_tag as $matchtag=>$addattary){
        if (preg_match($matchtag, $tagname)){
            $attary = array_merge($attary, $addattary);
        }
    }
    return $attary;
}

/**
 * This function filters url's
 *
 * @param  $attvalue        String with attribute value to filter
 * @param  $message         message object
 * @param  $id               message id
 * @param  $mailbox         mailbox
 * @param  $sQuote          quoting characters around url's
 */
function sq_fix_url($attname, &$attvalue, $message, $id, $mailbox,$sQuote = '"') {
}

/**
 * This function edits the style definition to make them friendly and
 * usable in SquirrelMail.
 *
 * @param  $message  the message object
 * @param  $id       the message id
 * @param  $content  a string with whatever is between <style> and </style>
 * @param  $mailbox  the message mailbox
 * @return           a string with edited content.
 */
function sq_fixstyle($body, $pos, $message, $id, $mailbox){
    $me = 'sq_fixstyle';

    // workaround for </style> in between comments
    $iCurrentPos = $pos;
    $content = '';
    $sToken = '';
    $bSucces = false;
    $bEndTag = false;
    for ($i=$pos,$iCount=strlen($body);$i<$iCount;++$i) {
        $char = $body{$i};
        switch ($char) {
            case '<':
                $sToken = $char;
                break;
            case '/':
                 if ($sToken == '<') {
                    $sToken .= $char;
                    $bEndTag = true;
                 } else {
                    $content .= $char;
                 }
                 break;
            case '>':
                 if ($bEndTag) {
                    $sToken .= $char;
                    if (preg_match('/\<\/\s*style\s*\>/i',$sToken,$aMatch)) {
                        $newpos = $i + 1;
                        $bSucces = true;
                        break 2;
                    } else {
                        $content .= $sToken;
                    }
                    $bEndTag = false;
                 } else {
                    $content .= $char;
                 }
                 break;

            case '!':
                if ($sToken == '<') {
                    // possible comment
                    if (isset($body{$i+2}) && substr($body,$i,3) == '!--') {
                        $i = strpos($body,'-->',$i+3);
                        if ($i === false) { // no end comment
                            $i = strlen($body);
                        } else {
							// add length of '-->'
							$i = $i + 2;
						}
                        $sToken = '';
                    }
                } else {
                    $content .= $char;
                }
                break;

            default:
                if ($bEndTag) {
                    $sToken .= $char;
                } else {
                    $content .= $char;
                }
                break;
        }
    }
    if ($bSucces == FALSE){
        return array(FALSE, strlen($body));
    }

    /**
     * First look for general BODY style declaration, which would be
     * like so:
     * body {background: blah-blah}
     * and change it to .bodyclass so we can just assign it to a <div>
     */
    $content = preg_replace("|body(\s*\{.*?\})|si", ".bodyclass\\1", $content);
	// When using the secure images the sec_remove_eng.png needs run throug gettext.
    $secremoveimg = '../images/' . "sec_remove_eng.png";

    // first check for 8bit sequences and disallowed control characters
    if (preg_match('/[\16-\37\200-\377]+/',$content)) {
        $content = '<!-- style block removed by html filter due to presence of 8bit characters -->';
        return array($content, $newpos);
    }

    // IE Sucks hard. We have a special function for it.
    sq_fixIE_idiocy($content);

    // remove @import line
    $content = preg_replace("/^\s*(@import.*)$/mi","\n<!-- @import rules forbidden -->\n",$content);

    /**
     * Fix url('blah') declarations.
     */
    // translate ur\l and variations into url (IE parses that)
    // TODO check if the sq_fixIE_idiocy function already handles this.
    $content = preg_replace("/(\\\\)?u(\\\\)?r(\\\\)?l(\\\\)?/i",'url', $content);
    preg_match_all("/url\s*\((.+)\)/si",$content,$aMatch);
    if (count($aMatch)) {
        $aValue = $aReplace = array();
        foreach($aMatch[1] as $sMatch) {
            // url value
            $urlvalue = $sMatch;
            sq_fix_url('style',$urlvalue, $message, $id, $mailbox,"'");
            $aValue[] = $sMatch;
            $aReplace[] = $urlvalue;
        }
        $content = str_replace($aValue,$aReplace,$content);
    }

    /**
     * Remove any backslashes, entities, and extraneous whitespace.
     */
    $contentTemp = $content;
    sq_defang($contentTemp);
    sq_unspace($contentTemp);

    /**
     * Fix stupid css declarations which lead to vulnerabilities
     * in IE.
     *
     * Also remove "position" attribute, as it can easily be set
     * to "fixed" or "absolute" with "left" and "top" attributes
     * of zero, taking over the whole content frame.  It can also
     * be set to relative and move itself anywhere it wants to,
     * displaying content in areas it shouldn't be allowed to touch.
     */
    $match   = Array('/\/\*.*\*\//',
                    '/expression/i',
                    '/behaviou*r/i',
                    '/binding/i',
                    '/include-source/i',
                    '/javascript/i',
                    '/script/i',
                    '/position/i');
    $replace = Array('','idiocy', 'idiocy', 'idiocy', 'idiocy', 'idiocy', 'idiocy', '');
    $contentNew = preg_replace($match, $replace, $contentTemp);
    if ($contentNew !== $contentTemp) {
        // insecure css declarations are used. From now on we don't care
        // anymore if the css is destroyed by sq_deent, sq_unspace or sq_unbackslash
        $content = $contentNew;
    }
    return array($content, $newpos);
}


/**
 * This function changes the <body> tag into a <div> tag since we
 * can't really have a body-within-body.
 *
 * @param  $attary   an array of attributes and values of <body>
 * @param  $mailbox  mailbox we're currently reading (for cid2http)
 * @param  $message  current message (for cid2http)
 * @param  $id       current message id (for cid2http)
 * @return           a modified array of attributes to be set for <div>
 */
function sq_body2div($attary, $mailbox, $message, $id){
    $me = 'sq_body2div';
    $divattary = Array('class' => "'bodyclass'");
    $bgcolor = '#ffffff';
    $text = '#000000';
    $styledef = '';
    if (is_array($attary) && sizeof($attary) > 0){
        foreach ($attary as $attname=>$attvalue){
            $quotchar = substr($attvalue, 0, 1);
            $attvalue = str_replace($quotchar, "", $attvalue);
            switch ($attname){
                case 'background':
					/**
					 * Used to be used to convert cid urls into viewable images. Did not work for 
					 * our implementation so removed the functions and calls.
					 */
                    break;
                case 'bgcolor':
                    $styledef .= "background-color: $attvalue; ";
                    break;
                case 'text':
                    $styledef .= "color: $attvalue; ";
                    break;
            }
        }
        if (strlen($styledef) > 0){
            $divattary{"style"} = "\"$styledef\"";
        }
    }
    return $divattary;
}


/**
 * This is the main function and the one you should actually be calling.
 * There are several variables you should be aware of an which need
 * special description.
 *
 * Since the description is quite lengthy, see it here:
 * http://linux.duke.edu/projects/mini/htmlfilter/
 *
 * @param $body                 the string with HTML you wish to filter
 * @param $tag_list             see description above
 * @param $rm_tags_with_content see description above
 * @param $self_closing_tags    see description above
 * @param $force_tag_closing    see description above
 * @param $rm_attnames          see description above
 * @param $bad_attvals          see description above
 * @param $add_attr_to_tag      see description above
 * @param $message              message object
 * @param $id                   message id
 * @return                      sanitized html safe to show on your pages.
 */
function sq_sanitize($body,
                     $tag_list,
                     $rm_tags_with_content,
                     $self_closing_tags,
                     $force_tag_closing,
                     $rm_attnames,
                     $bad_attvals,
                     $add_attr_to_tag,
                     $message,
                     $id,
                     $mailbox
                     ){
    $me = 'sq_sanitize';
    $rm_tags = array_shift($tag_list);
    /**
     * Normalize rm_tags and rm_tags_with_content.
     */
    @array_walk($tag_list, 'sq_casenormalize');
    @array_walk($rm_tags_with_content, 'sq_casenormalize');
    @array_walk($self_closing_tags, 'sq_casenormalize');
    /**
     * See if tag_list is of tags to remove or tags to allow.
     * false  means remove these tags
     * true   means allow these tags
     */
    $curpos = 0;
    $open_tags = Array();
    $trusted = "\n<!-- begin sanitized html -->\n";
    $skip_content = false;
    /**
     * Take care of netscape's stupid javascript entities like
     * &{alert('boo')};
     */
    $body = preg_replace("/&(\{.*?\};)/si", "&amp;\\1", $body);

    while (($curtag = sq_getnxtag($body, $curpos)) != FALSE){
        list($tagname, $attary, $tagtype, $lt, $gt) = $curtag;
        $free_content = substr($body, $curpos, $lt-$curpos);

        /**
         * Take care of <style>
         */
        if ($tagname == "style" && $tagtype == 1){
			/**
			 * this checks for <p>CONTENT<style></style></p>
			 * so here when the current tag is <style> and there is some text/content before the start of that tag 
			 * then we would like to copy that to $trusted string.
			 */
			if($free_content != FALSE){
					$trusted .= $free_content;
				}

			list($style_content, $curpos) =
                sq_fixstyle($body, $gt+1, $message, $id, $mailbox);
			
            if ($style_content != FALSE){
                $trusted .= sq_tagprint($tagname, $attary, $tagtype);
                $trusted .= $style_content;
                $trusted .= sq_tagprint($tagname, false, 2);
            }
            continue;
        }
        if ($skip_content == false){
            $trusted .= $free_content;
        }
        if ($tagname != FALSE){
            if ($tagtype == 2){
                if ($skip_content == $tagname){
                    /**
                     * Got to the end of tag we needed to remove.
                     */
                    $tagname = false;
                    $skip_content = false;
                } else {
                    if ($skip_content == false){
                        if ($tagname == "body"){
                            $tagname = "div";
                        }
                        if (isset($open_tags{$tagname}) &&
                                $open_tags{$tagname} > 0){
                            $open_tags{$tagname}--;
                        } else {
                            $tagname = false;
                        }
                    }
                }
            } else {
                /**
                 * $rm_tags_with_content
                 */
                if ($skip_content == false){
                    /**
                     * See if this is a self-closing type and change
                     * tagtype appropriately.
                     */
                    if ($tagtype == 1
                            && in_array($tagname, $self_closing_tags)){
                        $tagtype = 3;
                    }
                    /**
                     * See if we should skip this tag and any content
                     * inside it.
                     */
                    if ($tagtype == 1 &&
                            in_array($tagname, $rm_tags_with_content)){
                        $skip_content = $tagname;
                    } else {
                        if (($rm_tags == false
                                    && in_array($tagname, $tag_list)) ||
                                ($rm_tags == true &&
                                 !in_array($tagname, $tag_list))){
                            $tagname = false;
                        } else {
                            /**
                             * Convert body into div.
                             */
                            if ($tagname == "body"){
                                $tagname = "div";
                                $attary = sq_body2div($attary, $mailbox,
                                        $message, $id);
                            }
                            if ($tagtype == 1){
                                if (isset($open_tags{$tagname})){
                                    $open_tags{$tagname}++;
                                } else {
                                    $open_tags{$tagname}=1;
                                }
                            }
                            /**
                             * This is where we run other checks.
                             */
                            if (is_array($attary) && sizeof($attary) > 0){
                                $attary = sq_fixatts($tagname,
                                                     $attary,
                                                     $rm_attnames,
                                                     $bad_attvals,
                                                     $add_attr_to_tag,
                                                     $message,
                                                     $id,
                                                     $mailbox
                                                     );
                            }
                        }
                    }
                }
            }
            if ($tagname != false && $skip_content == false){
                $trusted .= sq_tagprint($tagname, $attary, $tagtype);
            }
        }
        $curpos = $gt+1;
    }
    $trusted .= substr($body, $curpos, strlen($body)-$curpos);
    if ($force_tag_closing == true){
        foreach ($open_tags as $tagname=>$opentimes){
            while ($opentimes > 0){
                $trusted .= '</' . $tagname . '>';
                $opentimes--;
            }
        }
        $trusted .= "\n";
    }
    $trusted .= "<!-- end sanitized html -->\n";
    return $trusted;
}

/**
 * This is a wrapper function to call html sanitizing routines.
 *
 * @param  $body  the body of the message
 * @param  $id    the id of the message
 * @return        a string with html safe to display in the browser.
 */
function magicHTML($body, $id) {
    $mailbox = '';
    /**
     * Don't display attached images in HTML mode.
     */
    $attachment_common_show_images = false;
    $tag_list = Array(
            false,
            "object",
            "meta",
            "html",
            "head",
            "base",
            "link",
            "frame",
            "iframe",
            "plaintext",
            "marquee"
            );

    $rm_tags_with_content = Array(
            "script",
            "applet",
            "embed",
            "title",
            "frameset",
            "xmp",
            "xml"
            );

    $self_closing_tags =  Array(
            "img",
            "br",
            "hr",
            "input",
            "outbind"
            );

    $force_tag_closing = true;

    $rm_attnames = Array(
            "/.*/" =>
            Array(
                "/target/i",
                "/^on.*/i",
                "/^dynsrc/i",
                "/^data.*/i",
                "/^lowsrc.*/i"
                )
            );

    $secremoveimg = "../images/" . _("sec_remove_eng.png");
    $bad_attvals = Array(
            "/.*/" =>
            Array(
                "/^src|background/i" =>
                Array(
                    Array(
                        "/^([\'\"])\s*\S+script\s*:.*([\'\"])/si",
                        "/^([\'\"])\s*mocha\s*:*.*([\'\"])/si",
                        "/^([\'\"])\s*about\s*:.*([\'\"])/si"
                        ),
                    Array(
                        "\\1$secremoveimg\\2",
                        "\\1$secremoveimg\\2",
                        "\\1$secremoveimg\\2"
                        )
                    ),
                "/^href|action/i" =>
                Array(
                    Array(
                        "/^([\'\"])\s*\S+script\s*:.*([\'\"])/si",
                        "/^([\'\"])\s*mocha\s*:*.*([\'\"])/si",
                        "/^([\'\"])\s*about\s*:.*([\'\"])/si"
                        ),
                    Array(
                        "\\1#\\1",
                        "\\1#\\1",
                        "\\1#\\1"
                        )
                    ),
        "/^style/i" =>
            Array(
                Array(
                    "/\/\*.*\*\//",
                    "/expression/i",
                    "/binding/i",
                    "/behaviou*r/i",
                    "/include-source/i",

                    // position:relative can also be exploited
                    // to put content outside of email body area
                    // and position:fixed is similarly exploitable
                    // as position:absolute, so we'll remove it
                    // altogether....
                    //
                    // Does this screw up legitimate HTML messages?
                    // If so, the only fix I see is to allow position
                    // attributes (any values?  I think we still have
                    // to block static and fixed) only if $use_iframe
                    // is enabled (1.5.0+)
                    //
                    // was:   "/position\s*:\s*absolute/i",
                    //
                    "/position\s*:/i",

                    "/(\\\\)?u(\\\\)?r(\\\\)?l(\\\\)?/i",
                    "/url\s*\(\s*([\'\"])\s*\S+script\s*:.*([\'\"])\s*\)/si",
                    "/url\s*\(\s*([\'\"])\s*mocha\s*:.*([\'\"])\s*\)/si",
                    "/url\s*\(\s*([\'\"])\s*about\s*:.*([\'\"])\s*\)/si",
                    "/(.*)\s*:\s*url\s*\(\s*([\'\"]*)\s*\S+script\s*:.*([\'\"]*)\s*\)/si",
                    ),
                Array(
                    "",
                    "idiocy",
                    "idiocy",
                    "idiocy",
                    "idiocy",
                    "idiocy",
                    "url",
                    "url(\\1#\\1)",
                    "url(\\1#\\1)",
                    "url(\\1#\\1)",
                    "\\1:url(\\2#\\3)"
                    )
                )
            )
        );

    $view_unsafe_images = true;

    if (!$view_unsafe_images){
        /**
         * Remove any references to http/https if view_unsafe_images set
         * to false.
         */
        array_push($bad_attvals{'/.*/'}{'/^src|background/i'}[0],
                '/^([\'\"])\s*https*:.*([\'\"])/si');
        array_push($bad_attvals{'/.*/'}{'/^src|background/i'}[1],
                "\\1$secremoveimg\\1");
        array_push($bad_attvals{'/.*/'}{'/^style/i'}[0],
                '/url\([\'\"]?https?:[^\)]*[\'\"]?\)/si');
        array_push($bad_attvals{'/.*/'}{'/^style/i'}[1],
                "url(\\1$secremoveimg\\1)");
    }

    $add_attr_to_tag = Array(
            "/^a$/i" =>
            Array('target'=>'"_blank"',
                'title'=>'"'._("This external link will open in a new window").'"'
                )
            );
    $trusted = sq_sanitize($body,
                           $tag_list,
                           $rm_tags_with_content,
                           $self_closing_tags,
                           $force_tag_closing,
                           $rm_attnames,
                           $bad_attvals,
                           $add_attr_to_tag,
                           0,
                           $id,
                           $mailbox
                           );
    if (strpos($trusted,$secremoveimg)){
        $has_unsafe_images = true;
    }

    return $trusted;
}

?>