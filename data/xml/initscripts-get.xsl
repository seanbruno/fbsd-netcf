<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ipcalc = "http://redhat.com/xslt/netcf/ipcalc/1.0"
                extension-element-prefixes="ipcalc"
                version="1.0">

  <xsl:import href="util-get.xsl"/>

  <xsl:output method="xml" indent="yes"/>

  <xsl:template match="/">
    <forest>
      <xsl:apply-templates/>
    </forest>
  </xsl:template>

  <!-- Some global variables

       To keep my sanity, all global variables start with 'g_'
  -->
  <xsl:variable name="g_startmode" select="/interface/start/@mode"/>
  <xsl:variable name="g_mtu" select="/interface/mtu/@size"/>

  <!--
      Ethernet (physical interface)
  -->
  <xsl:template match="/interface[@type = 'ethernet']">
    <tree>
      <xsl:call-template name="bare-ethernet-interface"/>
      <xsl:call-template name="interface-addressing"/>
    </tree>
  </xsl:template>

  <!--
      VLAN's
  -->
  <xsl:template match="interface[@type = 'vlan']" name="vlan-interface">
    <tree>
      <xsl:call-template name="vlan-interface-common"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <!-- nothing to do for vlan-device -->
    </tree>
  </xsl:template>

  <xsl:template name="vlan-interface-common">
    <xsl:variable name="iface" select="concat(vlan/interface/@name, '.', vlan/@tag)"/>

    <xsl:attribute name="path">/files/etc/sysconfig/network-scripts/ifcfg-<xsl:value-of select="$iface"/></xsl:attribute>
    <node label="DEVICE" value="{$iface}"/>
    <node label="VLAN" value="yes"/>
  </xsl:template>

  <xsl:template name='bare-vlan-interface'>
    <xsl:call-template name='vlan-interface-common'/>
    <xsl:call-template name="startmode"/>
    <xsl:call-template name="mtu"/>
    <!-- nothing to do for vlan-device -->
  </xsl:template>

  <!--
      Bridge
  -->
  <xsl:template match="/interface[@type = 'bridge']">
    <tree>
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <node label="TYPE" value="Bridge"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:if test="bridge/@stp">
        <node label="STP" value="{bridge/@stp}"/>
      </xsl:if>
      <xsl:if test="bridge/@delay">
        <node label="DELAY" value="{bridge/@delay}"/>
      </xsl:if>
    </tree>
    <xsl:for-each select='bridge/interface'>
      <tree>
        <xsl:if test="@type = 'ethernet'">
          <xsl:call-template name="bare-ethernet-interface"/>
        </xsl:if>
        <xsl:if test="@type = 'vlan'">
          <xsl:call-template name="bare-vlan-interface"/>
        </xsl:if>
        <xsl:if test="@type = 'bond'">
          <xsl:call-template name="bare-bond-interface"/>
        </xsl:if>
        <node label="BRIDGE" value="{../../@name}"/>
      </tree>
      <xsl:if test="@type = 'bond'">
        <xsl:call-template name="bond-slaves"/>
      </xsl:if>
    </xsl:for-each>
  </xsl:template>

  <!--
      Bond
  -->
  <xsl:template name="bond-slaves">
    <xsl:for-each select='bond/interface'>
      <tree>
        <xsl:call-template name="bare-ethernet-interface"/>
        <node label="MASTER" value="{../../@name}"/>
        <node label="SLAVE" value="yes"/>
      </tree>
    </xsl:for-each>
  </xsl:template>

  <xsl:template name="bonding-opts-node">
    <node label="BONDING_OPTS">
      <xsl:attribute name="value">
        <xsl:call-template name="bonding-opts"/>
      </xsl:attribute>
    </node>
  </xsl:template>

  <xsl:template name="bare-bond-interface">
    <xsl:call-template name="name-attr"/>
    <xsl:call-template name="startmode"/>
    <xsl:call-template name="mtu"/>
    <xsl:call-template name="bonding-opts-node"/>
  </xsl:template>

  <xsl:template match="/interface[@type = 'bond']">
    <tree>
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:call-template name="bonding-opts-node"/>
    </tree>
    <xsl:call-template name="bond-slaves"/>
  </xsl:template>

  <!--
       Named templates, following the Relax NG syntax
  -->
  <xsl:template name="name-attr">
    <xsl:attribute name="path">/files/etc/sysconfig/network-scripts/ifcfg-<xsl:value-of select="@name"/></xsl:attribute>
    <node label="DEVICE" value="{@name}"/>
  </xsl:template>

  <xsl:template name="mtu">
    <xsl:if test="$g_mtu != ''">
      <node label="MTU" value="{$g_mtu}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <xsl:call-template name="name-attr"/>
    <xsl:if test="mac">
      <node label="HWADDR" value="{mac/@address}"/>
    </xsl:if>
    <xsl:call-template name="startmode"/>
    <xsl:call-template name="mtu"/>
  </xsl:template>

  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="$g_startmode = 'onboot'">
        <node label="ONBOOT" value="yes"/>
      </xsl:when>
      <xsl:when test="$g_startmode = 'none'">
        <node label="ONBOOT" value="no"/>
      </xsl:when>
      <xsl:when test="$g_startmode = 'hotplug'">
        <node label="ONBOOT" value="no"/>
        <node label="HOTPLUG" value="yes"/>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:for-each select="protocol[@family='ipv4']">
      <xsl:call-template name="protocol-ipv4"/>
    </xsl:for-each>
    <xsl:for-each select="protocol[@family='ipv6']">
      <xsl:call-template name="protocol-ipv6"/>
    </xsl:for-each>
  </xsl:template>

  <xsl:template name="protocol-ipv4">
    <xsl:choose>
      <xsl:when test="dhcp">
        <node label="BOOTPROTO" value="dhcp"/>
        <xsl:if test="dhcp/@peerdns">
          <node label="PEERDNS" value="{dhcp/@peerdns}"/>
        </xsl:if>
      </xsl:when>
      <xsl:when test="ip">
        <node label="BOOTPROTO" value="none"/>
        <node label="IPADDR" value="{ip/@address}"/>
        <xsl:if test="ip/@prefix">
          <node label="NETMASK" value="{ipcalc:netmask(ip/@prefix)}"/>
        </xsl:if>
        <xsl:if test="route">
          <node label="GATEWAY" value="{route/@gateway}"/>
        </xsl:if>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="protocol-ipv6">
    <node label="IPV6INIT" value="yes"/>
    <xsl:if test="count(autoconf) > 0">
      <node label="IPV6_AUTOCONF" value="yes"/>
    </xsl:if>
    <xsl:if test="count(autoconf) = 0">
      <node label="IPV6_AUTOCONF" value="no"/>
    </xsl:if>
    <xsl:if test="count(dhcp) > 0">
      <node label="DHCPV6" value="yes"/>
    </xsl:if>
    <xsl:if test="count(dhcp) = 0">
      <node label="DHCPV6" value="no"/>
    </xsl:if>
    <xsl:if test="count(ip) > 0">
      <node label="IPV6ADDR">
        <xsl:attribute name="value">
          <xsl:value-of select="ip[1]/@address"/><xsl:if test="ip[1]/@prefix">/<xsl:value-of select="ip[1]/@prefix"/></xsl:if>
        </xsl:attribute>
      </node>
    </xsl:if>
    <xsl:if test="count(ip) > 1">
      <node label="IPV6ADDR_SECONDARIES">
        <xsl:attribute name="value">
          <xsl:text>'</xsl:text>
          <xsl:for-each select="ip[1]/following-sibling::ip[following-sibling::ip]">
            <xsl:value-of select="@address"/><xsl:if test="@prefix">/<xsl:value-of select="@prefix"/></xsl:if><xsl:value-of select="string(' ')"/>
          </xsl:for-each>
          <xsl:for-each select="ip[last()]">
            <xsl:value-of select="@address"/><xsl:if test="@prefix">/<xsl:value-of select="@prefix"/></xsl:if>
          </xsl:for-each>
          <xsl:text>'</xsl:text>
        </xsl:attribute>
      </node>
    </xsl:if>
    <xsl:if test="route">
      <node label="IPV6_DEFAULTGW" value="{route/@gateway}"/>
    </xsl:if>
  </xsl:template>

</xsl:stylesheet>
