@@ expression E; @@
- if (E)
(
-	free(E);
+ free(E);
|
-	MAPIFreeBuffer(E);
+ MAPIFreeBuffer(E);
)
