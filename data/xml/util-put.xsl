<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:bond = "http://redhat.com/xslt/netcf/bond/1.0"
                extension-element-prefixes="bond"
                version="1.0">

  <xsl:template name="bonding-opts">
    <xsl:param name="opts" />
    <xsl:variable name="mode" select="bond:option($opts, 'mode')"/>
    <xsl:if test="string-length($mode) > 0">
      <xsl:attribute name="mode">
        <xsl:choose>
          <xsl:when test="$mode = '0'">balance-rr</xsl:when>
          <xsl:when test="$mode = '1'">active-backup</xsl:when>
          <xsl:when test="$mode = '2'">balance-xor</xsl:when>
          <xsl:when test="$mode = '3'">broadcast</xsl:when>
          <xsl:when test="$mode = '4'">802.3ad</xsl:when>
          <xsl:when test="$mode = '5'">balance-tlb</xsl:when>
          <xsl:when test="$mode = '6'">balance-alb</xsl:when>
          <xsl:otherwise>
            <xsl:value-of select="$mode"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:attribute>
    </xsl:if>
    <xsl:if test="bond:option($opts, 'miimon')">
      <miimon freq="{bond:option($opts, 'miimon')}">
        <xsl:if test="bond:option($opts, 'downdelay')"><xsl:attribute name="downdelay"><xsl:value-of select="bond:option($opts, 'downdelay')"/></xsl:attribute></xsl:if>
        <xsl:if test="bond:option($opts, 'updelay')"><xsl:attribute name="updelay"><xsl:value-of select="bond:option($opts, 'updelay')"/></xsl:attribute></xsl:if>
        <xsl:if test="bond:option($opts, 'use_carrier')">
          <xsl:attribute name="carrier">
            <xsl:if test="bond:option($opts, 'use_carrier') = '0'">ioctl</xsl:if>
            <xsl:if test="bond:option($opts, 'use_carrier') = '1'">netif</xsl:if>
          </xsl:attribute>
        </xsl:if>
      </miimon>
    </xsl:if>
    <xsl:if test="bond:option($opts, 'arp_interval')">
      <arpmon interval="{bond:option($opts, 'arp_interval')}" target="{bond:option($opts, 'arp_ip_target')}">
        <xsl:variable name="val" select="bond:option($opts, 'arp_validate')"/>
        <xsl:if test="$val">
          <xsl:attribute name="validate">
            <xsl:if test="$val = 'none' or $val = '0'">none</xsl:if>
            <xsl:if test="$val = 'active' or $val = '1'">active</xsl:if>
            <xsl:if test="$val = 'backup' or $val = '2'">backup</xsl:if>
            <xsl:if test="$val = 'all' or $val = '3'">all</xsl:if>
          </xsl:attribute>
        </xsl:if>
      </arpmon>
    </xsl:if>
  </xsl:template>
</xsl:stylesheet>
