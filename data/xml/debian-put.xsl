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

  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="/">
    <xsl:apply-templates select="forest/tree[@path='/files/etc/network/interfaces']/array[@label='iface']/element[1]" mode='iface'/>
  </xsl:template>

  <xsl:template match="element" mode="ifacetype">
    <xsl:choose>
      <xsl:when test="count(node[@label='bridge_ports']) > 0">
        <xsl:text>bridge</xsl:text>
      </xsl:when>
      <xsl:when test="count(node[@label='bond_slaves']) > 0">
        <xsl:text>bond</xsl:text>
      </xsl:when>
      <xsl:when test="count(node[@label='vlan_raw_device']) > 0">
        <xsl:text>vlan</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>ethernet</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="element" mode="ifacestart">
    <xsl:variable name='key' select="@key"/>
    <xsl:if test="count(../../array[@label='auto']/element/node[@value=$key]) > 0">
      <start mode="onboot"/>
    </xsl:if>
    <xsl:if test="count(../../array[@label='auto']/element/node[@value=$key]) = 0">
      <start mode="none"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="ifacemisc">
    <xsl:if test="count(node[@label='hwaddress' and substring(@value, 1, 6) = 'ether ']) > 0">
      <mac address="{substring(node[@label='hwaddress']/@value, 7)}"/>
    </xsl:if>
    <xsl:if test="count(node[@label='mtu']) > 0">
      <mtu size="{node[@label='mtu']/@value}"/>
    </xsl:if>
  </xsl:template>
  <!--
      Ethernet adapter
  -->
  <xsl:template match="element" mode="iface">
    <interface name="{@key}">
      <xsl:attribute name='type'>
        <xsl:apply-templates select="." mode="ifacetype"/>
      </xsl:attribute>
      <xsl:apply-templates select="." mode="ifacestart"/>
      <xsl:apply-templates select="." mode="ifacemisc"/>
      <xsl:apply-templates select="." mode='protocol'/>
      <xsl:apply-templates select="." mode='bridge'/>
      <xsl:apply-templates select="." mode='bond'/>
      <xsl:apply-templates select="." mode='vlan'/>
    </interface>
  </xsl:template>

  <xsl:template match="element" mode="protocol">
    <xsl:choose>
      <xsl:when test="count(node[@label='family' and @value='inet']) > 0">
        <xsl:apply-templates select="." mode="ipv4"/>
        <xsl:apply-templates select="../element[count(node[@label='family' and @value='inet6'][1])>0]" mode='ipv6'/>
      </xsl:when>
      <xsl:when test="count(node[@label='family' and @value='inet6']) > 0">
        <xsl:apply-templates select="." mode="ipv6"/>
        <xsl:apply-templates select="../element[count(node[@label='family' and @value='inet4'][1])>0]" mode='ipv4'/>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="element" mode="ipv4">
    <xsl:choose>
      <xsl:when test="node[@label='method']/@value = 'static'">
        <protocol family='ipv4'>
          <xsl:apply-templates select="." mode="ipv4static"/>
        </protocol>
      </xsl:when>
      <xsl:when test="node[@label='method']/@value = 'dhcp'">
        <protocol family='ipv4'>
          <xsl:apply-templates select="." mode="ipv4dhcp"/>
        </protocol>
      </xsl:when>
      <xsl:when test="node[@label='method']/@value = 'loopback'">
        <protocol family='ipv4'>
          <xsl:apply-templates select="." mode="ipv4lo"/>
        </protocol>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="element" mode="ipv4static">
    <xsl:if test="count(node[@label='address']) > 0">
      <ip address="{node[@label='address']/@value}">
        <xsl:if test="count(node[@label='netmask']) > 0">
          <xsl:attribute name="prefix">
            <xsl:value-of select="ipcalc:prefix(node[@label='netmask']/@value)"/>
          </xsl:attribute>
        </xsl:if>
      </ip>
    </xsl:if>
    <xsl:if test="count(node[@label='gateway']) > 0">
      <route gateway="{node[@label='gateway']/@value}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="ipv4lo">
    <ip address="127.0.0.1" prefix="8"/>
  </xsl:template>

  <xsl:template match="element" mode="ipv4dhcp">
    <dhcp/> <!-- peerdns='no'/>-->
  </xsl:template>

  <xsl:template match="element" mode="ipv6">
    <protocol family='ipv6'>
      <xsl:apply-templates select="." mode="ipv6static"/>
      <xsl:apply-templates select="." mode="ipv6autoconf"/>
      <xsl:apply-templates select="." mode="ipv6dhcp"/>
      <xsl:apply-templates select="." mode="ipv6lo"/>
    </protocol>
  </xsl:template>

  <xsl:template match="element" mode="ipv6static">
    <xsl:if test="count(node[@label='address']) > 0">
      <ip address="{node[@label='address']/@value}" prefix="{node[@label='netmask']/@value}"/>
    </xsl:if>
    <xsl:if test="count(node[@label='gateway']) > 0">
      <route gateway="{node[@label='gateway']/@value}"/>
    </xsl:if>

    <xsl:for-each select="node[@label='up']">
      <xsl:variable name="ipaddcmd" select="concat('/sbin/ifconfig ', ../@key, ' inet6 add ')"/>

      <xsl:if test="starts-with(@value, $ipaddcmd)">
        <xsl:variable name="ipaddrprefix" select="substring(@value, string-length($ipaddcmd)+1)"/>
        <ip address="{substring-before($ipaddrprefix, '/')}" prefix="{substring-after($ipaddrprefix, '/')}"/>
      </xsl:if>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="element" mode="ipv6lo">
    <xsl:if test="node[@label='method']/@value = 'loopback'">
      <ip address="::1" prefix="8"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="ipv6dhcp">
    <xsl:if test="node[@label='method']/@value = 'dhcp'">
      <dhcp/> <!-- peerdns='no'/>-->
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="ipv6autoconf">
    <xsl:variable name="autoconfcmd" select="concat('echo 0 > /proc/sys/net/ipv6/conf/', @key, '/autoconf')"/>
    <xsl:if test="count(node[@label='method' and @value='loopback']) = 0 and count(node[@label='pre-up' and @value = $autoconfcmd]) = 0">
      <autoconf/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="bridge">
    <xsl:if test="count(node[@label='bridge_ports']) > 0">
      <bridge>
        <xsl:if test="count(node[@label='bridge_stp']) > 0">
          <xsl:attribute name='stp'>
            <xsl:value-of select="node[@label='bridge_stp']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:if test="count(node[@label='bridge_fd']) > 0">
          <xsl:attribute name='delay'>
            <xsl:value-of select="node[@label='bridge_fd']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:variable name="cur" select=".."/>
        <xsl:if test="node[@label='bridge_ports']/@value != 'none'">
          <xsl:for-each select="str:tokenize(node[@label='bridge_ports']/@value, ' ')">
            <xsl:variable name="port" select="."/>
            <interface name='{$port}'>
              <xsl:attribute name="type">
                <xsl:choose>
                  <xsl:when test="count($cur/element[@key=$port]) > 0">
                    <xsl:apply-templates select="$cur/element[@key=$port]" mode="ifacetype"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:text>ethernet</xsl:text>
                  </xsl:otherwise>
                </xsl:choose>
              </xsl:attribute>
              <xsl:apply-templates select="$cur/element[@key=$port]" mode="ifacemisc"/>
              <xsl:apply-templates select="$cur/element[@key=$port]" mode='bond'/>
              <xsl:apply-templates select="$cur/element[@key=$port]" mode='vlan'/>
            </interface>
          </xsl:for-each>
        </xsl:if>
      </bridge>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="bond">
    <xsl:if test="count(node[@label='bond_slaves']) > 0">
      <bond>
        <xsl:if test="count(node[@label='bond_mode']) > 0">
          <xsl:attribute name='mode'>
            <xsl:value-of select="node[@label='bond_mode']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:apply-templates select="." mode="bondmiimon"/>
        <xsl:apply-templates select="." mode="bondarpmon"/>
        <xsl:for-each select="str:tokenize(node[@label='bond_slaves']/@value, ' ')">
          <interface type='ethernet' name='{.}'/>
        </xsl:for-each>
      </bond>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="bondmiimon">
    <xsl:if test="count(node[@label='bond_miimon']) > 0">
      <miimon freq="{node[@label='bond_miimon']/@value}">
        <xsl:if test="count(node[@label='bond_updelay']) > 0">
          <xsl:attribute name='updelay'>
            <xsl:value-of select="node[@label='bond_updelay']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:if test="count(node[@label='bond_downdelay']) > 0">
          <xsl:attribute name='downdelay'>
            <xsl:value-of select="node[@label='bond_downdelay']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:if test="count(node[@label='bond_use_carrier']) > 0">
          <xsl:attribute name='carrier'>
            <xsl:if test="node[@label='bond_use_carrier'] != 0">
              <xsl:text>ioctl</xsl:text>
            </xsl:if>
            <xsl:if test="node[@label='bond_use_carrier'] = 0">
              <xsl:text>netif</xsl:text>
            </xsl:if>
          </xsl:attribute>
        </xsl:if>
      </miimon>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="bondarpmon">
    <xsl:if test="count(node[@label='bond_arp_ip_target']) > 0">
      <arpmon target="{node[@label='bond_arp_ip_target']/@value}">
        <xsl:if test="count(node[@label='bond_arp_interval']) > 0">
          <xsl:attribute name='interval'>
            <xsl:value-of select="node[@label='bond_arp_interval']/@value"/>
          </xsl:attribute>
        </xsl:if>
        <xsl:if test="count(node[@label='bond_arp_validate']) > 0">
          <xsl:attribute name='validate'>
            <xsl:value-of select="node[@label='bond_arp_validate']/@value"/>
          </xsl:attribute>
        </xsl:if>
      </arpmon>
    </xsl:if>
  </xsl:template>

  <xsl:template match="element" mode="vlan">
    <xsl:if test="count(node[@label='vlan_raw_device']) > 0">
      <vlan tag="{substring(@key, string-length(node[@label='vlan_raw_device']/@value)+2)}">
        <interface name="{node[@label='vlan_raw_device']/@value}"/>
      </vlan>
    </xsl:if>
  </xsl:template>
</xsl:stylesheet>
