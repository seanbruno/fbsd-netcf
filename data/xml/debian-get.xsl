<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ipcalc = "http://redhat.com/xslt/netcf/ipcalc/1.0"
                extension-element-prefixes="ipcalc"
                version="1.0">

  <xsl:import href="util-get.xsl"/>

  <xsl:output method="xml" indent="yes"/>

  <xsl:template match="/">
    <forest>
      <tree path="/files/etc/network/interfaces">
        <xsl:apply-templates/>
      </tree>
    </forest>
  </xsl:template>

  <xsl:template match="interface">
    <xsl:if test="./start/@mode = 'onboot'">
      <array label="auto">
        <element>
          <node value="{@name}"/>
        </element>
      </array>
    </xsl:if>
    <array label="iface">
      <xsl:for-each select="protocol">
        <element key="{../@name}">
          <xsl:apply-templates select="."/>
          <xsl:if test="position() = 1">
            <xsl:apply-templates select="../mtu|../mac"/>
          </xsl:if>
          <xsl:if test="position() = 1">
            <xsl:apply-templates select="../bridge|../bond|../vlan"/>
            <xsl:for-each select="../bridge/interface[count(bond|vlan)>0]">
              <node label="pre-up" value="ifup {@name}"/>
              <node label="post-down" value="ifdown {@name}"/>
            </xsl:for-each>
          </xsl:if>
        </element>
      </xsl:for-each>
      <xsl:if test="count(protocol) = 0">
        <element key="{@name}">
          <node label="family" value='inet'/>
          <node label='method' value='manual'/>
          <xsl:apply-templates select="mtu|mac"/>
          <xsl:apply-templates select="bridge|bond|vlan"/>
          <xsl:for-each select="bridge/interface[count(bond)>0]">
            <node label="pre-up" value="ifup {@name}"/>
            <node label="post-down" value="ifdown {@name}"/>
          </xsl:for-each>
        </element>
      </xsl:if>
      <xsl:for-each select="bridge/interface[count(bond|vlan)>0]">
        <element key="{@name}">
          <node label="family" value='inet'/>
          <node label="method" value='manual'/>
          <xsl:apply-templates select="bond|vlan"/>
        </element>
      </xsl:for-each>
    </array>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv4' and count(dhcp) > 0]">
    <node label='family' value='inet'/>
    <node label='method' value='dhcp'/>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv4' and count(ip) > 0]">
    <node label='family' value='inet'/>
    <node label='method' value='static'/>
    <xsl:apply-templates select="ip[1]|route|address" mode='ipv4'/>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv4' and count(ip|dhcp) = 0]">
    <node label='family' value='inet'/>
    <node label='method' value='manual'/>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv6' and count(dhcp) > 0]">
    <node label='family' value='inet6'/>
    <node label='method' value='dhcp'/>
    <xsl:apply-templates select="." mode="autoconf"/>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv6' and count(ip) > 0]">
    <node label='family' value='inet6'/>
    <node label='method' value='static'/>
    <xsl:apply-templates select="ip[1]|route|address" mode='ipv6'/>
    <xsl:apply-templates select="." mode="autoconf"/>
    <xsl:for-each select="ip[position()>1]">
      <node label="up" value="/sbin/ifconfig {../../@name} inet6 add {@address}/{@prefix}"/>
    </xsl:for-each>
    <xsl:for-each select="ip[position()>1]">
      <node label="down" value="/sbin/ifconfig {../../@name} inet6 del {@address}/{@prefix}"/>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv6' and count(ip|dhcp) = 0]">
    <node label='family' value='inet6'/>
    <node label='method' value='manual'/>
    <xsl:apply-templates select="." mode="autoconf"/>
  </xsl:template>

  <xsl:template match="protocol[@family='ipv6']" mode="autoconf">
    <xsl:if test="count(autoconf) = 0">
      <node label='pre-up' value='echo 0 > /proc/sys/net/ipv6/conf/{../@name}/autoconf'/>
      <node label='post-down' value='echo 1 > /proc/sys/net/ipv6/conf/{../@name}/autoconf'/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="ip" mode='ipv4'>
    <node label="address" value="{@address}"/>
    <xsl:if test="@prefix">
      <node label="netmask" value="{ipcalc:netmask(@prefix)}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="ip" mode='ipv6'>
    <node label="address" value="{@address}"/>
    <xsl:if test="@prefix">
      <node label="netmask" value="{@prefix}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="mtu">
    <node label="mtu" value="{@size}"/>
  </xsl:template>

  <xsl:template match="mac">
    <node label="hwaddress" value="ether {@address}"/>
  </xsl:template>

  <xsl:template match="bridge">
    <xsl:choose>
      <xsl:when test="count(interface) > 0">
        <node label="bridge_ports">
          <xsl:attribute name="value">
            <xsl:for-each select="interface">
              <xsl:if test="position() != 1">
                <xsl:text> </xsl:text>
              </xsl:if>
              <xsl:value-of select="@name"/>
            </xsl:for-each>
          </xsl:attribute>
        </node>
      </xsl:when>
      <xsl:otherwise>
        <node label="bridge_ports" value="none"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="@stp">
      <node label="bridge_stp" value="{@stp}"/>
    </xsl:if>
    <xsl:if test="@delay">
      <node label="bridge_fd" value="{@delay}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="bond">
    <xsl:choose>
      <xsl:when test="count(interface) > 0">
        <node label="bond_slaves">
          <xsl:attribute name="value">
            <xsl:for-each select="interface">
              <xsl:if test="position() != 1">
                <xsl:text> </xsl:text>
              </xsl:if>
              <xsl:value-of select="@name"/>
            </xsl:for-each>
          </xsl:attribute>
        </node>
        <node label="bond_primary" value="{interface[1]/@name}"/>
      </xsl:when>
      <xsl:otherwise>
        <node label="bond_slaves" value="none"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="@mode">
      <node label="bond_mode" value="{@mode}"/>
    </xsl:if>
    <xsl:apply-templates select="miimon|arpmon"/>
  </xsl:template>

  <xsl:template match="miimon">
    <node label="bond_miimon" value="{@freq}"/>
    <xsl:if test="@downdelay">
      <node label="bond_downdelay" value="{@downdelay}"/>
    </xsl:if>
    <xsl:if test="@updelay">
      <node label="bond_updelay" value="{@updelay}"/>
    </xsl:if>
    <xsl:if test="@carrier='ioctl'">
      <node label="bond_use_carrier" value="0"/>
    </xsl:if>
    <xsl:if test="@carrier='netif'">
      <node label="bond_use_carrier" value="1"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="arpmon">
    <node label="bond_arp_interval" value="{@interval}"/>
    <node label="bond_arp_ip_target" value="{@target}"/>
    <xsl:if test="@validate">
      <node label="bond_arp_validate" value="{@validate}"/>
    </xsl:if>
  </xsl:template>

  <xsl:template match="vlan">
    <node label="vlan_raw_device" value="{interface/@name}"/>
  </xsl:template>

  <xsl:template match="route[@gateway]" mode='ipv4'>
    <node label="gateway" value="{@gateway}"/>
  </xsl:template>
  <xsl:template match="route[@gateway]" mode='ipv6'>
    <node label="gateway" value="{@gateway}"/>
  </xsl:template>


</xsl:stylesheet>
