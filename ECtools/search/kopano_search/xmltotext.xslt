<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
	<xsl:output method="text" standalone="no" indent="yes" encoding="utf-8" />
	<xsl:template match="text()|@*">
		<xsl:value-of select="."/><xsl:text> </xsl:text>
	</xsl:template>
</xsl:stylesheet>
