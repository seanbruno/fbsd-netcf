<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ipcalc = "http://redhat.com/xslt/netcf/ipcalc/1.0"
                xmlns:bond = "http://redhat.com/xslt/netcf/bond/1.0"
                extension-element-prefixes="bond ipcalc"
                version="1.0">

  <xsl:output method="xml" indent="yes"/>
  <xsl:strip-space elements="*"/>

  <xsl:template match="/">
    <xsl:apply-templates select="*"/>
  </xsl:template>

  <!--
      Ethernet adapter
  -->
  <xsl:template name="ethernet-interface"
                match="tree[count(node[@label = 'MASTER' or @label='BRIDGE']) = 0]">
    <interface type="ethernet">
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="basic-ethernet-content"/>
      <xsl:call-template name="interface-addressing"/>
    </interface>
  </xsl:template>

  <!--
      Bridge
  -->
  <xsl:template name="bridge-interface"
                match="tree[node[@label = 'TYPE' and @value = 'Bridge']]">
    <interface type="bridge">
      <!-- the bridge node itself -->
      <xsl:call-template name="startmode"/>
      <xsl:variable name="iface" select="node[@label= 'DEVICE']/@value"/>
      <xsl:call-template name="basic-attrs"/>
      <xsl:call-template name="interface-addressing"/>
      <bridge>
        <xsl:if test="node[@label = 'STP']">
          <xsl:attribute name="stp"><xsl:value-of select="node[@label = 'STP']/@value"/></xsl:attribute>
        </xsl:if>
        <xsl:for-each select="/descendant-or-self::*[node[@label = 'BRIDGE' and @value = $iface]]">
          <xsl:call-template name="bare-ethernet-interface"/>
        </xsl:for-each>
      </bridge>
    </interface>
  </xsl:template>

  <!--
      Bond
  -->
  <xsl:template name="bond-interface"
                match="tree[node[@label = 'DEVICE'][@value = //tree/node[@label = 'MASTER']/@value]]">
    <interface type="bond">
      <xsl:call-template name="startmode"/>
      <xsl:variable name="iface" select="node[@label= 'DEVICE']/@value"/>
      <xsl:call-template name="basic-attrs"/>
      <xsl:call-template name="interface-addressing"/>
      <bond>
        <xsl:variable name="mode" select="bond:option(node[@label = 'BONDING_OPTS']/@value, 'mode')"/>
        <xsl:variable name="primary" select="bond:option(node[@label = 'BONDING_OPTS']/@value, 'primary')"/>
        <xsl:if test="string-length($mode) > 0">
          <xsl:attribute name="mode">
            <xsl:value-of select="$mode"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:for-each select="/descendant-or-self::*[node[@label = 'MASTER' and @value = $iface]][node[@label = 'DEVICE' and @value = $primary]]">
          <xsl:call-template name='bare-ethernet-interface'/>
        </xsl:for-each>
        <xsl:for-each select="/descendant-or-self::*[node[@label = 'MASTER' and @value = $iface]][node[@label = 'DEVICE' and @value != $primary]]">
          <xsl:call-template name='bare-ethernet-interface'/>
        </xsl:for-each>
      </bond>
    </interface>
  </xsl:template>

  <!--
       Utility templates, names follow the names in interface.rng
  -->
  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="node[@label ='HOTPLUG']/@value = 'yes'">
        <xsl:attribute name="startmode">hotplug</xsl:attribute>
      </xsl:when>
      <xsl:when test="node[@label = 'ONBOOT']/@value = 'yes'">
        <xsl:attribute name="startmode">onboot</xsl:attribute>
      </xsl:when>
      <xsl:otherwise>
        <xsl:attribute name="startmode">none</xsl:attribute>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="basic-attrs">
    <name><xsl:value-of select="node[@label= 'DEVICE']/@value"/></name>
    <xsl:if test="node[@label='MTU']">
      <mtu><xsl:attribute name="size"><xsl:value-of select="node[@label='MTU']/@value"/></xsl:attribute></mtu>
    </xsl:if>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:choose>
      <xsl:when test="node[@label = 'BOOTPROTO']/@value = 'dhcp'">
        <dhcp>
          <xsl:if test="node[@label = 'PEERDNS']">
            <xsl:attribute name="peerdns"><xsl:value-of select="node[@label = 'PEERDNS']/@value"></xsl:value-of></xsl:attribute>
          </xsl:if>
        </dhcp>
      </xsl:when>
      <xsl:when test="node[@label = 'BOOTPROTO']/@value = 'none'">
        <ip>
          <xsl:attribute name="address"><xsl:value-of select="node[@label = 'IPADDR']/@value"/></xsl:attribute>
          <xsl:if test="node[@label = 'NETMASK']">
            <xsl:attribute name="prefix"><xsl:value-of select="ipcalc:prefix(node[@label = 'NETMASK']/@value)"/></xsl:attribute>
          </xsl:if>
        </ip>
        <xsl:if test="node[@label = 'GATEWAY']">
          <route>
            <xsl:attribute name="gateway"><xsl:value-of select="node[@label = 'GATEWAY']/@value"/></xsl:attribute>
          </route>
        </xsl:if>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="basic-ethernet-content">
    <xsl:call-template name="basic-attrs"/>
    <xsl:if test="node[@label = 'HWADDR']">
      <mac>
        <xsl:attribute name="address"><xsl:value-of select="node[@label = 'HWADDR']/@value"/></xsl:attribute>
      </mac>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <interface type="ethernet">
      <xsl:call-template name="basic-ethernet-content"/>
    </interface>
  </xsl:template>
</xsl:stylesheet>
