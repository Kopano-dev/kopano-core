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

## User Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/user.md)

[List Users](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list.md)

[Get User](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_get.md)

[List Messages](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_messages.md)

[List mailFolders](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_mailfolders.md)

[sendMail](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_sendmail.md)

[List contactFolders](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_contactfolders.md)

[List calendarView](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_list_calendarview.md)

[delta](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/user_delta.md)

## Message Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/message.md)

[createReply](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_createreply.md)

[delta](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/message_delta.md)

## Subscription Resource

[(Resource)](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/resources/subscription.md)

[Create Subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_post_subscriptions.md)

[Get Subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_get.md)

[Delete Subscription](https://github.com/microsoftgraph/microsoft-graph-docs/blob/master/api-reference/v1.0/api/subscription_delete.md)
