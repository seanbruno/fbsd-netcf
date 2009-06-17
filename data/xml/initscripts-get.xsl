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
      Bridge
  -->
  <xsl:template match="/interface[@type = 'bridge']">
    <tree>
      <xsl:call-template name="basic-attrs"/>
      <node label="TYPE" value="Bridge"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="interface-addressing"/>
      <xsl:if test="bridge/@stp">
        <node label="STP">
          <xsl:attribute name="value"><xsl:value-of select="bridge/@stp"/></xsl:attribute>
        </node>
      </xsl:if>
    </tree>
    <xsl:for-each select='bridge/interface'>
      <tree>
        <xsl:call-template name="bare-ethernet-interface"/>
        <node label="BRIDGE">
          <xsl:attribute name="value"><xsl:value-of select="../../name"/></xsl:attribute>
        </node>
      </tree>
    </xsl:for-each>
  </xsl:template>

  <!--
      Bond
  -->
  <xsl:template match="/interface[@type = 'bond']">
    <tree>
      <xsl:call-template name="basic-attrs"/>
      <xsl:call-template name="startmode"/>
      <xsl:call-template name="interface-addressing"/>
      <node label="BONDING_OPTS">
        <xsl:attribute name="value">'<xsl:if test="bond/@mode">mode=<xsl:value-of select='bond/@mode'/></xsl:if><xsl:if test="bond/@mode = 'active-backup'"> primary=<xsl:value-of select='bond/interface[1]/name'/></xsl:if>'</xsl:attribute>
      </node>
    </tree>
    <xsl:for-each select='bond/interface'>
      <tree>
        <xsl:call-template name="bare-ethernet-interface"/>
        <node label="MASTER">
          <xsl:attribute name="value"><xsl:value-of select="../../name"/></xsl:attribute>
        </node>
        <node label="SLAVE" value="yes"/>
      </tree>
    </xsl:for-each>
  </xsl:template>

  <!--
       Named templates, following the Relax NG syntax
  -->
  <xsl:template name="basic-attrs">
    <xsl:attribute name="path">/files/etc/sysconfig/network-scripts/ifcfg-<xsl:value-of select="name"/></xsl:attribute>
    <node label="DEVICE">
      <xsl:attribute name="value"><xsl:value-of select="name"/></xsl:attribute>
    </node>
    <xsl:if test="mtu">
      <node label="MTU">
        <xsl:attribute name="value"><xsl:value-of select="mtu/@size"/></xsl:attribute>
      </node>
    </xsl:if>
  </xsl:template>

  <xsl:template name="bare-ethernet-interface">
    <xsl:call-template name="basic-attrs"/>
    <xsl:if test="mac">
      <node label="HWADDR">
        <xsl:attribute name="value"><xsl:value-of select="mac/@address"/></xsl:attribute>
      </node>
    </xsl:if>
  </xsl:template>

  <xsl:template name="startmode">
    <xsl:choose>
      <xsl:when test="@startmode = 'onboot'">
        <node label="ONBOOT" value="yes"/>
      </xsl:when>
      <xsl:when test="@startmode = 'none'">
        <node label="ONBOOT" value="no"/>
      </xsl:when>
      <xsl:when test="@startmode = 'hotplug'">
        <node label="ONBOOT" value="no"/>
        <node label="HOTPLUG" value="yes"/>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="interface-addressing">
    <xsl:choose>
      <xsl:when test="dhcp">
        <node label="BOOTPROTO" value="dhcp"/>
        <xsl:if test="dhcp/@peerdns">
          <node label="PEERDNS">
            <xsl:attribute name="value"><xsl:value-of select="dhcp/@peerdns"/></xsl:attribute>
          </node>
        </xsl:if>
      </xsl:when>
      <xsl:when test="ip">
        <node label="BOOTPROTO" value="none"/>
        <node label="IPADDR">
          <xsl:attribute name="value"><xsl:value-of select="ip/@address"/></xsl:attribute>
        </node>
        <node label="NETMASK">
          <xsl:attribute name="value"><xsl:value-of select="ipcalc:netmask(ip/@prefix)"/></xsl:attribute>
        </node>
        <xsl:if test="route">
          <node label="GATEWAY">
            <xsl:attribute name="value"><xsl:value-of select="route/@gateway"/></xsl:attribute>
          </node>
        </xsl:if>
      </xsl:when>
    </xsl:choose>
  </xsl:template>
</xsl:stylesheet>
