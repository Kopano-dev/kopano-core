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
	 * Journal Module
	 */
	class JournalListModule extends ListModule
	{
		/**
		 * @var Array properties of mail item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function JournalListModule($id, $data)
		{
			$this->tablecolumns = $GLOBALS["TableColumns"]->getJournalListTableColumns();

			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->start = 0;
		}

		/**
		 * Function will generate property tags based on passed MAPIStore to use
		 * in module. These properties are regenerated for every request so stores
		 * residing on different servers will have proper values for property tags.
		 * @param MAPIStore $store store that should be used to generate property tags.
		 * @param Binary $entryid entryid of message/folder
		 * @param Array $action action data sent by client
		 */
		function generatePropertyTags($store, $entryid, $action)
		{
			$this->properties = $GLOBALS["properties"]->getMailProperties($store);

			$this->sort = array();
			$this->sort[$this->properties["message_delivery_time"]] = TABLE_SORT_DESCEND;
		}
	}
?>
