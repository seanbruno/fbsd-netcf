<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ipcalc = "http://redhat.com/xslt/netcf/ipcalc/1.0"
                xmlns:bond = "http://redhat.com/xslt/netcf/bond/1.0"
                xmlns:str="http://exslt.org/strings"
                extension-element-prefixes="bond ipcalc str"
                version="1.0">

  <xsl:import href="util-put.xsl"/>

  <xsl:output method="xml" indent="yes"/>
  <xsl:strip-space elements="*"/>

  <xsl:template match="/">
    <xsl:apply-templates select="*"/>
  </xsl:template>

  <!--
      Ethernet adapter
  -->
  <xsl:template name="ethernet-interface"
                match="tree[count(node[@label = 'MASTER' or @label='BRIDGE'
                                       or @label = 'VLAN']) = 0]">
    <interface type="ethernet">
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:if test="node[@label = 'HWADDR']">
        <mac address="{node[@label = 'HWADDR']/@value}"/>
      </xsl:if>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
    </interface>
  </xsl:template>

  <!--
      VLAN's
  -->
  <xsl:template name="vlan-interface"
                match="tree[node[@label = 'VLAN' and @value = 'yes']][count(node[@label = 'MASTER' or @label='BRIDGE']) = 0]">
    <interface type="vlan">
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:call-template name="vlan-device"/>
    </interface>
  </xsl:template>

  <xsl:template name="bare-vlan-interface">
    <xsl:variable name="name" select="node[@label = 'DEVICE']/@value"/>
    <interface type="vlan">
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="vlan-device"/>
    </interface>
  </xsl:template>

  <xsl:template name="vlan-device">
    <xsl:variable name="name" select="node[@label = 'DEVICE']/@value"/>
    <xsl:variable name="device" select="substring-before($name, '.')"/>
    <xsl:variable name="tag" select="substring-after($name, '.')"/>
    <vlan tag="{$tag}">
      <interface name="{$device}"/>
    </vlan>
  </xsl:template>

  <!--
      Bridge
  -->
  <xsl:template name="bridge-interface"
                match="tree[node[@label = 'TYPE' and @value = 'Bridge']]">
    <interface type="bridge">
      <!-- the bridge node itself -->
      <xsl:variable name="iface" select="node[@label= 'DEVICE']/@value"/>
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <bridge>
        <xsl:if test="node[@label = 'STP']">
          <xsl:attribute name="stp"><xsl:value-of select="node[@label = 'STP']/@value"/></xsl:attribute>
        </xsl:if>
        <xsl:if test="node[@label = 'DELAY']">
          <xsl:attribute name="delay"><xsl:value-of select="node[@label = 'DELAY']/@value"/></xsl:attribute>
        </xsl:if>
        <xsl:for-each select="/descendant-or-self::*[node[@label = 'BRIDGE' and @value = $iface]]">
          <xsl:if test="count(node[@label = 'VLAN' or @label = 'BONDING_OPTS']) = 0">
            <xsl:call-template name="bare-ethernet-interface"/>
          </xsl:if>
          <xsl:if test="count(node[@label = 'BONDING_OPTS']) > 0">
            <xsl:call-template name="bare-bond-interface"/>
          </xsl:if>
          <xsl:if test="count(node[@label = 'VLAN']) > 0">
            <xsl:call-template name="bare-vlan-interface"/>
          </xsl:if>
        </xsl:for-each>
      </bridge>
    </interface>
  </xsl:template>

  <!--
      Bond
  -->
  <xsl:template name="bond-element">
    <xsl:variable name="iface" select="node[@label= 'DEVICE']/@value"/>
    <bond>
      <xsl:variable name="opts" select="node[@label = 'BONDING_OPTS']/@value"/>
      <xsl:call-template name="bonding-opts">
        <xsl:with-param name="opts" select="$opts"/>
      </xsl:call-template>
      <xsl:variable name="primary" select="bond:option($opts, 'primary')"/>
      <xsl:for-each select="/descendant-or-self::*[node[@label = 'MASTER' and @value = $iface]][node[@label = 'DEVICE' and @value = $primary]]">
        <xsl:call-template name='bare-ethernet-interface'/>
      </xsl:for-each>
      <xsl:for-each select="/descendant-or-self::*[node[@label = 'MASTER' and @value = $iface]][node[@label = 'DEVICE' and @value != $primary]]">
        <xsl:call-template name='bare-ethernet-interface'/>
      </xsl:for-each>
    </bond>
  </xsl:template>

  <xsl:template name="bare-bond-interface">
    <interface type="bond">
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="bond-element"/>
    </interface>
  </xsl:template>

  <xsl:template name="bond-interface"
                match="tree[node[@label = 'DEVICE'][@value = //tree/node[@label = 'MASTER']/@value]][count(node[@label = 'BRIDGE']) = 0]">
    <interface type="bond">
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:call-template name="bond-element"/>
    </interface>
  </xsl:template>

  <!--
       Utility templates, names follow the names in interface.rng
  -->
  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="node[@label ='STARTMODE']/@value = 'yes'">
        <start mode='hotplug'/>
      </xsl:when>
      <xsl:when test="node[@label = 'STARTMODE']/@value = 'auto'">
        <start mode='onboot'/>
      </xsl:when>
      <xsl:when test="node[@label = 'STARTMODE']/@value = 'manual'">
        <start mode='manual'/>
      </xsl:when>
      <xsl:otherwise>
        <start mode='none'/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="name-attr">
    <xsl:attribute name="name">
      <xsl:value-of select="node[@label= 'DEVICE']/@value"/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template name="mtu">
    <xsl:if test="node[@label='MTU']">
      <mtu size="{node[@label='MTU']/@value}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:call-template name="protocol-ipv4"/>
    <xsl:call-template name="protocol-ipv6"/>
  </xsl:template>

  <xsl:template name="protocol-ipv4">
    <xsl:variable name="uses_dhcp"
                  select="node[@label = 'BOOTPROTO']/@value = 'dhcp'"/>
    <xsl:variable name="uses_static"
                  select="count(node[@label = 'IPADDR']) > 0"/>
    <xsl:variable name="uses_ipv4" select="$uses_dhcp or $uses_static"/>
    <xsl:if test="$uses_ipv4">
    <protocol family="ipv4">
      <xsl:choose>
        <xsl:when test="$uses_dhcp">
          <dhcp>
            <xsl:if test="node[@label = 'PEERDNS']">
              <xsl:attribute name="peerdns"><xsl:value-of select="node[@label = 'PEERDNS']/@value"></xsl:value-of></xsl:attribute>
            </xsl:if>
          </dhcp>
        </xsl:when>
        <xsl:when test="$uses_static">
          <ip address="{node[@label = 'IPADDR']/@value}">
            <xsl:choose>
              <xsl:when test="node[@label = 'PREFIX']">
                <xsl:attribute name="prefix"><xsl:value-of select="node[@label = 'PREFIX']/@value"/></xsl:attribute>
              </xsl:when>
              <xsl:when test="node[@label = 'NETMASK']">
                <xsl:attribute name="prefix"><xsl:value-of select="ipcalc:prefix(node[@label = 'NETMASK']/@value)"/></xsl:attribute>
              </xsl:when>
            </xsl:choose>
          </ip>
          <xsl:if test="node[@label = 'GATEWAY']">
            <route gateway="{node[@label = 'GATEWAY']/@value}"/>
          </xsl:if>
        </xsl:when>
      </xsl:choose>
    </protocol>
    </xsl:if>
  </xsl:template>

  <xsl:template name="protocol-ipv6">
    <xsl:if test="node[@label = 'IPV6INIT'][@value = 'yes']">
      <protocol family="ipv6">
        <xsl:if test="node[@label = 'IPV6_AUTOCONF'][@value = 'yes']">
          <autoconf/>
        </xsl:if>
        <xsl:if test="node[@label = 'DHCPV6'][@value = 'yes']">
          <dhcp/>
        </xsl:if>
        <xsl:if test="node[@label = 'IPV6ADDR']">
          <xsl:call-template name="ipv6-address">
            <xsl:with-param name="value"
                            select="node[@label = 'IPV6ADDR']/@value"/>
          </xsl:call-template>
        </xsl:if>
        <xsl:if test="node[@label = 'IPV6ADDR_SECONDARIES']">
          <!-- Strip surrounding single quotes from $s -->
          <xsl:variable name="s"
                        select="node[@label = 'IPV6ADDR_SECONDARIES']/@value"/>
          <xsl:variable name="q">'</xsl:variable>
          <xsl:variable name="sec">
            <xsl:choose>
              <xsl:when test="starts-with($s, $q)">
                <xsl:value-of select="substring($s, 2, string-length($s)-2)"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="$s"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:for-each select="str:split($sec)">
            <xsl:call-template name="ipv6-address">
              <xsl:with-param name="value" select="."/>
            </xsl:call-template>
          </xsl:for-each>
        </xsl:if>
      </protocol>
    </xsl:if>
  </xsl:template>

  <xsl:template name="ipv6-address">
    <xsl:param name="value"/>
    <xsl:variable name="address" select="substring-before($value, '/')"/>
    <xsl:variable name="prefix" select="substring-after($value, '/')"/>
    <ip address="{$address}">
      <xsl:if test="$prefix != ''">
        <xsl:attribute name="prefix"><xsl:value-of select="$prefix"/></xsl:attribute>
      </xsl:if>
    </ip>
    <xsl:if test="node[@label = 'IPV6_DEFAULTGW']">
      <route gateway="{node[@label = 'IPV6_DEFAULTGW']/@value}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <interface type="ethernet">
      <xsl:call-template name="name-attr"/>
      <xsl:if test="node[@label = 'HWADDR']">
        <mac address="{node[@label = 'HWADDR']/@value}"/>
      </xsl:if>
    </interface>
  </xsl:template>
</xsl:stylesheet>
