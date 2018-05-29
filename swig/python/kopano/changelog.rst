==========
Python-kopano Change Log
==========

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
