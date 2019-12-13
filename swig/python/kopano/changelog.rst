==========
Python-kopano Change Log
==========

`8.7.1`_ (2019-04-19)
-------------------------
* Optimized user listing pagination
* Improved some meetingrequest-related properties
* Fix regression introduced in 8.7.0, for recurrences
  and busystatus
* Further minor fixes

`8.7.0`_ (2019-01-21)
-------------------------

* Remove deprecated Body class
* Convert some MAPI exceptions to Kopano exceptions

* Greatly improved notifications (per store, folder,
  type of event, ..)
* Added KQL query language support. for example one can
  now use:
  folder.items(query='from:bert AND size>1MB')
  server.users(query='frits')
* Added Server.sync_gab (user directory sync)
* Greatly improved Recurrence attributes,
  for reading/changing/creating recurrences.
  Also hiding internals much better now.
* Added general 'kopano' logger, and log several
  new warnings (more to come)
* Generalized saving: retry on deadlocks/temp issues
* Improved timezone handling, using Olson db
* Many improvements to meeting request handling

* Added Contact.{email2, address2, ..}
* Added Item.{urgency, read_receipt} setters
* Improved Item.create_item (embedded message)
* Added Item.body_preview (optimized for bulk)
* Added User.{first_name, mobile_phone, ..}
* Added Picture class
* Added {User, Contact}.photo (Picture instance)
* Added ArgumentError, and use in many places
* Added Appointment.{location, create_attendee, ..}
* Added Appointment.{accept, decline}
* Added hidden, active filters to Server.users()
* Expand stubbed messages when dumping
* Added Appointment.{cancel, canceled}
* Improvements to Occurrence class (more attributes)
* Added Appointment.{reminder, reminder_minutes}
* Very basic Rule management (loop, create, delete)
* Added Item.{codepage, encoding, html_utf8}
* Added Attachment.{hidden, inline, content_id,
  content_location}
* Added Store.add_favorite()
* Added Store.add_search() (permanent search folders)
* Added Item.type_ ('mail', 'contact', ..)
* Improve command-line boolean format (yes/no/true/
  false, case insensitive)

* Optimized Folder/Recurrence occurrences
* Optimized Property attributes

* Fullname default to name for Server.create_user()
* Company.stores() also yields public store now
* Make sure named props are always created on server
* Keep working when extended exceptions are missing
* Improved cleanup of unused Store objects
* Fixed broken logging in item.dump(s)()
* Fixed Folder.delete(occurrence)
* Fixes for Item.create_reply()
* Fix for tracking tab visibility
* Fixed Item.{to, cc, bcc} setters
* More circular import fixes for Python ~3.4
* Fix for reminder times (and timezones)

`8.6.1`_ (2018-04-04)
-------------------------

* Improved compatibility with Python 3.4
  (circular imports)


`8.6.0`_ (2016-03-08)
---------------------

* Fixed Item.{copy,move} to save item
* Deprecated item.importance

* Improved interactive shell autocompletion
* Added Recurrence.{pattern, weekdays, index,
  interval, month, monthday, first_weekday,
  range_type}
* Added Item.{sent, changekey, has_attachments,
  messageid, conversationid, urgency, read_receipt,
  delivery_receipt, filtered_html, sensitivity,
  searchkey, reply}
* Added Item.reply(all), to create a reply.
* Added Item.{sender} setter
* Added Store.{mail_folders, contact_folders, calendars}
* Added kopano.set_bin_encoding('hex'/'base64')
  (so all identifiers are in hex/urlsafe-base64)
* Added Attachment.entryid, item.attachment(entryid)
* Added Attachment.{embedded, item, last_modified}
* Added Attachment.mimetype setter
* Added Folder.subfolder_count_recursive
* Added Attendee class, Appointment.attendees
* Added Attendee.{address, response, response_time,
  type}
* Added Appointment.{reminder, reminder_minutes,
  all_day, show_as, response_requested, icaluid}
* Added kopano.pidlid (named property definitions)
* Added Contact.{initials, given_name, first_name,
  middle_name, last_name, nickname, title, generation,
  company_name, children, spouse, birthday,
  yomi_first_name, yomi_last_name, yomi_company_name,
  mobile_phone, file_as, job_title, department,
  office_location, profession, manager, assistant,
  business_homepage, home_phones, business_phones,
  im_addresses, home_address, business_address,
  other_address}
* Added {Item, Folder}.delete(occurrence)
* Added Occurrence.{subject, start, end, location},
  plus setters
