<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ipcalc = "http://redhat.com/xslt/netcf/ipcalc/1.0"
                extension-element-prefixes="ipcalc"
                version="1.0">

  <xsl:output method="xml" indent="yes"/>

  <xsl:template match="/">
    <forest>
      <xsl:apply-templates/>
    </forest>
  </xsl:template>

  <!--
      Ethernet (physical interface)
  -->
  <xsl:template match="/interface[@type = 'ethernet']">
    <tree>
      <xsl:call-template name="bare-ethernet-interface"/>
      <xsl:call-template name="startmode"/>
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
    </tree>
    <xsl:for-each select='bridge/interface'>
      <tree>
        <xsl:call-template name="bare-ethernet-interface"/>
        <node label="BRIDGE" value="{../../@name}"/>
      </tree>
    </xsl:for-each>
  </xsl:template>

  <!--
      Bond
  -->
  <xsl:template match="/interface[@type = 'bond']">
    <tree>
      <xsl:call-template name="name-attr"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="mtu"/>
      <xsl:call-template name="interface-addressing"/>
      <node label="BONDING_OPTS">
        <xsl:attribute name="value">
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
        </xsl:attribute>
      </node>
    </tree>
    <xsl:for-each select='bond/interface'>
      <tree>
        <xsl:call-template name="bare-ethernet-interface"/>
        <node label="MASTER" value="{../../@name}"/>
        <node label="SLAVE" value="yes"/>
      </tree>
    </xsl:for-each>
  </xsl:template>

  <!--
       Named templates, following the Relax NG syntax
  -->
  <xsl:template name="name-attr">
    <xsl:attribute name="path">/files/etc/sysconfig/network-scripts/ifcfg-<xsl:value-of select="@name"/></xsl:attribute>
    <node label="DEVICE" value="{@name}"/>
  </xsl:template>

  <xsl:template name="mtu">
    <xsl:if test="mtu">
      <node label="MTU" value="{mtu/@size}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <xsl:call-template name="name-attr"/>
    <xsl:if test="mac">
      <node label="HWADDR" value="{mac/@address}"/>
    </xsl:if>
    <xsl:call-template name="mtu"/>
  </xsl:template>

  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="start/@mode = 'onboot'">
        <node label="ONBOOT" value="yes"/>
      </xsl:when>
      <xsl:when test="start/@mode = 'none'">
        <node label="ONBOOT" value="no"/>
      </xsl:when>
      <xsl:when test="start/@mode = 'hotplug'">
        <node label="ONBOOT" value="no"/>
        <node label="HOTPLUG" value="yes"/>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:for-each select="protocol[@family='ipv4']">
      <xsl:call-template name="protocol-ipv4"/>
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
</xsl:stylesheet>
