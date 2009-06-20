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
        <xsl:variable name="opts" select="node[@label = 'BONDING_OPTS']/@value"/>
        <xsl:variable name="mode" select="bond:option($opts, 'mode')"/>
        <xsl:variable name="primary" select="bond:option($opts, 'primary')"/>
        <xsl:if test="string-length($mode) > 0">
          <xsl:attribute name="mode">
            <xsl:value-of select="$mode"/>
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
      <mtu size="{node[@label='MTU']/@value}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:call-template name="protocol-ipv4"/>
  </xsl:template>

  <xsl:template name="protocol-ipv4">
    <protocol family="ipv4">
      <xsl:choose>
        <xsl:when test="node[@label = 'BOOTPROTO']/@value = 'dhcp'">
          <dhcp>
            <xsl:if test="node[@label = 'PEERDNS']">
              <xsl:attribute name="peerdns"><xsl:value-of select="node[@label = 'PEERDNS']/@value"></xsl:value-of></xsl:attribute>
            </xsl:if>
          </dhcp>
        </xsl:when>
        <xsl:when test="node[@label = 'BOOTPROTO']/@value = 'none'">
          <ip address="{node[@label = 'IPADDR']/@value}">
            <xsl:if test="node[@label = 'NETMASK']">
              <xsl:attribute name="prefix"><xsl:value-of select="ipcalc:prefix(node[@label = 'NETMASK']/@value)"/></xsl:attribute>
            </xsl:if>
          </ip>
          <xsl:if test="node[@label = 'GATEWAY']">
            <route gateway="{node[@label = 'GATEWAY']/@value}"/>
          </xsl:if>
        </xsl:when>
      </xsl:choose>
    </protocol>
  </xsl:template>

  <xsl:template name="basic-ethernet-content">
    <xsl:call-template name="basic-attrs"/>
    <xsl:if test="node[@label = 'HWADDR']">
      <mac address="{node[@label = 'HWADDR']/@value}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <interface type="ethernet">
      <xsl:call-template name="basic-ethernet-content"/>
    </interface>
  </xsl:template>
</xsl:stylesheet>
