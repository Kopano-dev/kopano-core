General deviations from Microsoft Graph:

1. Endpoints such as "/me/messages" or "/me/events" do not expose the entire store, but just the inbox or calendar, respectively.
2. We do not support relative paths, such as "mailFolder/id/childFolder/id/childFolder/id/.."
3. Certain esoteric OData fields such as @odata.context are not yet correctly exported.
4. Many fields are not exported, such as user or contact fields, or cannot be changed at the moment.

Extensions:

1. We support handling attachments in binary using $value, using for example: "GET /me/messages/id/attachment/id/$value".

Query Parameters:

[Graph documentation](https://developer.microsoft.com/en-us/graph/docs/concepts/query_parameters)

1. We do not support $filter or $format.
2. Support for $search is very preliminary (no support for [KQL](https://docs.microsoft.com/en-us/sharepoint/dev/general-development/keyword-query-language-kql-syntax-reference) or anything).
3. Support for $expand and $count is preliminary.

## attachment Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/attachment.md)

[Get attachment](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/attachment_get.md)

(Extension: use attachment/id/$value to get attachment in binary.)

[Delete attachment](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/attachment_delete.md)

## calendar Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/calendar.md)

[Get calendar](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/calendar_get.md)

[List calendarView](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/calendar_list_calendarview.md)

[List events](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/calendar_list_events.md)

[Create event](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/calendar_post_events.md)

## contact Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/contact.md)

[Get contact](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contact_get.md)

[Delete contact](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contact_delete.md)

[delta](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contact_delta.md)

## contactFolder Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/contactfolder.md)

[Get contactFolder](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contactfolder_get.md)

[List contacts](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contactfolder_list_contacts.md)

[Create contact](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/contactfolder_post_contacts.md)

## event Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/event.md)

[Get event](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_get.md)

[Update event](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_update.md)

[Delete event](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_delete.md)

[List instances](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_list_instances.md)

[List attachments](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_list_attachments.md)

[Add attachment](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/event_post_attachments.md)

## mailFolder Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/mailfolder.md)

[Get mailFolder](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_get.md)

[Create childFolder](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_post_childfolders.md)

[List childFolders](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_list_childfolders.md)

[Create message](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_post_messages.md)

[List messages](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_list_messages.md)

[copy](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_copy.md)

[move](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/mailfolder_move.md)

## message Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/message.md)

[Get message](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_get.md)

[Update message](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_update.md)

[Delete message](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_delete.md)

[createReply](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_createreply.md)

[createReplyAll](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_createreplyall.md)

[List attachments](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_list_attachments.md)

[Add attachment](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_post_attachments.md)

[copy](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_copy.md)

[move](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_move.md)

[delta](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_delta.md)

## profilePhoto Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/profilephoto.md)

[Get profilePhoto](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/profilephoto_get.md)

[Update profilePhoto](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/profilephoto_update.md)

## subscription Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/subscription.md)

[Create subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_post_subscriptions.md)

[Get subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_get.md)

[Delete subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_delete.md)

## user Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/user.md)

[List users](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list.md)

[Get user](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_get.md)

[List messages](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_messages.md)

[Create message](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_post_messages.md)

[List mailFolders](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_mailfolders.md)

[Create mailFolder](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_post_mailfolders.md)

[sendMail](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_sendmail.md)

[List events](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_events.md)

[Create event](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_post_events.md)

[List contactFolders](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_contactfolders.md)

[List calendars](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_calendars.md)

[List calendarView](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_calendarview.md)

[List contacts](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_contacts.md)

[Create contact](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_post_contacts.md)

[delta](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_delta.md)

[calendar](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/calendar.md)

[calendars](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/calendar.md)

[events](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/event.md)
