<?xml version="1.0" encoding="UTF-8"?>
<!--
$Id$
-->
<xsl:stylesheet 
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" 
    xmlns:xs="http://www.w3.org/2001/XMLSchema"
    xmlns:mfunc="http://exslt.org/math"
    xmlns:dfunc="http://exslt.org/dates-and-times"
    version="1.0">
<xsl:output method="html"/>
    
    <xsl:template match="/">
        <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" /> 
        <style type="text/css">
            table {
                width:800px;
                border-collapse:collapse;
                margin-left:5px;
                margin-top:5px;
            }
            table td {
                padding-left:10px;
                padding-right:10px;            
            }
            table th {
                border-top:solid gray 2px;
                border-bottom:solid gray 1px;
                font-family:sans-serif;
                color:#fff;
                font-weight:normal;
                background:#9CA68D;
            }
            .tbl-cell-l {
                border-left:solid gray 2px;
                border-right:dashed gray 1px;
            }
            .tbl-cell-i {
                border-right:dashed gray 1px;
            }
            .tbl-cell-r {
                border-right:solid gray 2px;
                border-left:dashed gray 1px;
            }
        </style>
        <xsl:apply-templates />
    </xsl:template>
    
    <xsl:template match="tvpvrdhistory">
        <table>
            <thead>
                <tr>
                    <th class="tbl-cell-l">Date</th>
                    <th class="tbl-cell-i">Time</th>
                    <th class="tbl-cell-i">Title</th>                    
                    <th class="tbl-cell-r">File</th>
                </tr>
            </thead>
            <tbody>
                <xsl:apply-templates />
            </tbody>
        </table>
    </xsl:template>
     
    <xsl:template match="recording">
        <tr>
            <td class="tbl-cell-l">
                <xsl:value-of select="datestart"/>
                <!--<xsl:value-of select="dfunc:add('1970-01-01T01:00:00',dfunc:duration(timestampstart))"/>-->
            </td>
            <td class="tbl-cell-i">
                <xsl:value-of select="timestart"/>
                <xsl:text> - </xsl:text>
                <xsl:value-of select="timeend"/>
                <!--<xsl:value-of select="dfunc:add('1970-01-01T01:00:00',dfunc:duration(timestampend))"/>-->
            </td>
            <td class="tbl-cell-i" style="font-weight:bold;">
                <xsl:value-of select="title"/>
            </td>            
            <td class="tbl-cell-r">
                <xsl:value-of select="filepath"/>
            </td>
        </tr>        
    </xsl:template>

</xsl:stylesheet>
