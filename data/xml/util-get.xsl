<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">

  <xsl:template name="bonding-opts">
    <xsl:text>'</xsl:text>
    <xsl:if test="bond/@mode">mode=<xsl:value-of select='bond/@mode'/></xsl:if>
    <xsl:if test="bond/@mode = 'active-backup'"> primary=<xsl:value-of select='bond/interface[1]/@name'/></xsl:if>
    <xsl:if test="bond/miimon">
      <xsl:text> miimon=</xsl:text><xsl:value-of select='bond/miimon/@freq'/>
      <xsl:if test="bond/miimon/@downdelay"><xsl:text> downdelay=</xsl:text><xsl:value-of select="bond/miimon/@downdelay"/></xsl:if>
      <xsl:if test="bond/miimon/@updelay"><xsl:text> updelay=</xsl:text><xsl:value-of select="bond/miimon/@updelay"/></xsl:if>
      <xsl:if test="bond/miimon/@carrier">
        <xsl:text> use_carrier=</xsl:text>
        <xsl:if test="bond/miimon/@carrier = 'ioctl'">0</xsl:if>
        <xsl:if test="bond/miimon/@carrier = 'netif'">1</xsl:if>
      </xsl:if>
    </xsl:if>
    <xsl:if test="bond/arpmon">
      <xsl:text> arp_interval=</xsl:text><xsl:value-of select="bond/arpmon/@interval"/>
      <xsl:text> arp_ip_target=</xsl:text><xsl:value-of select="bond/arpmon/@target"/>
      <xsl:if test="bond/arpmon/@validate"><xsl:text> arp_validate=</xsl:text><xsl:value-of select="bond/arpmon/@validate"/></xsl:if>
    </xsl:if>
    <xsl:text>'</xsl:text>
  </xsl:template>

</xsl:stylesheet>
