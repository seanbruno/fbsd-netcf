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

    <xsl:attribute name="path">/files/etc/sysconfig/network/ifcfg-<xsl:value-of select="$iface"/></xsl:attribute>
    <node label="DEVICE" value="{$iface}"/>
    <node label="ETHERDEVICE" value="{vlan/interface/@name}"/>
    <!-- <node label="VLAN" value="yes"/> -->
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
      <node label="BRIDGE" value="yes"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:if test="bridge/@stp">
        <node label="BRIDGE_STP" value="{bridge/@stp}"/>
      </xsl:if>
      <xsl:if test="bridge/@delay">
        <node label="BRIDGE_FORWARDDELAY" value="{bridge/@delay}"/>
      </xsl:if>
      <xsl:variable name="bridges">
        <xsl:for-each select="//bridge/interface">
        <xsl:text> </xsl:text><xsl:value-of select="@name"/>
        </xsl:for-each>
      </xsl:variable>
      <node label="BRIDGE_PORTS"> <xsl:attribute name="value"> <xsl:value-of select="$bridges"/></xsl:attribute></node>
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
      <node label="BONDING_MASTER" value="yes"/>
      <xsl:for-each select="//bond/interface">
        <node>
          <xsl:attribute name="label">BONDING_SLAVE_<xsl:copy-of select='position()-1'/></xsl:attribute>
          <xsl:attribute name="value"><xsl:value-of select="@name"/></xsl:attribute>
      </node>
      </xsl:for-each>
      <xsl:call-template name="bonding-opts-node"/>
    </tree>
    <xsl:call-template name="bond-slaves"/>
  </xsl:template>

  <!--
       Named templates, following the Relax NG syntax
  -->
  <xsl:template name="name-attr">
    <xsl:attribute name="path">/files/etc/sysconfig/network/ifcfg-<xsl:value-of select="@name"/></xsl:attribute>
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
      <node label="BOOTPROTO" value="none"/>
  </xsl:template>

  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="$g_startmode = 'onboot'">
        <node label="STARTMODE" value="auto"/>
      </xsl:when>
      <xsl:when test="$g_startmode = 'none'">
        <node label="STARTMODE" value="manual"/>
      </xsl:when>
      <xsl:when test="$g_startmode = 'hotplug'">
        <node label="STARTMODE" value="ifplugd"/>
      </xsl:when>
      <xsl:otherwise>
        <node label="STARTMODE" value="manual"/>
      </xsl:otherwise>
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
        <!-- <xsl:if test="dhcp/@peerdns">
          <node label="PEERDNS" value="{dhcp/@peerdns}"/>
        </xsl:if> -->
      </xsl:when>
      <xsl:when test="ip">
        <node label="BOOTPROTO" value="static"/>
        <node label="IPADDR" value="{ip/@address}"/>
        <xsl:if test="ip/@prefix">
          <node label="NETMASK" value="{ipcalc:netmask(ip/@prefix)}"/>
        </xsl:if>
        <xsl:if test="route">
          <node label="GATEWAY" value="{route/@gateway}"/>
        </xsl:if>
      </xsl:when>
      <xsl:otherwise>
        <node label="BOOTPROTO" value="none"/>
      </xsl:otherwise>
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
