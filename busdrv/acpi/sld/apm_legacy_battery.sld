<?xml version="1.0" encoding="UTF-16"?>
<!DOCTYPE DCARRIER SYSTEM "mantis.dtd">
<DCARRIER CarrierRevision="1">
	<TOOLINFO ToolName="INF2SLD"><![CDATA[<?xml version="1.0" encoding="UTF-16"?>
<!DOCTYPE TOOL SYSTEM "tool.dtd">
<TOOL>
	<CREATED><NAME>INF2SLD</NAME><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>7/16/2001</DATE><PROPERTY Name="INF_Src" Format="String">battery.inf</PROPERTY></CREATED><LASTSAVED><NAME>INF2SLD</NAME><VERSION>1.0.0.452</VERSION><BUILD>452</BUILD><DATE>7/16/2001</DATE></LASTSAVED></TOOL>
]]></TOOLINFO><COMPONENT Revision="1" Visibility="1000" MultiInstance="0" Released="0" Editable="1" HTMLFinal="0" ComponentVSGUID="{185AD391-E19D-4346-92FB-075347124A2E}" ComponentVIGUID="{FDAF1721-8755-4206-9C04-D5D7D640A420}" PlatformGUID="{B784E719-C196-4DDB-B358-D9254426C38D}" RepositoryVSGUID="{8E0BE9ED-7649-47F3-810B-232D36C430B4}" PrototypeVIGUID="{E65EA663-D0C1-4b65-A3C5-369FB269EB1C}"><PROPERTY Name="cmiPnPDevId" Format="String">NTAPM\APMBATT</PROPERTY><PROPERTY Name="cmiPnPDevInf" Format="String">battery.inf</PROPERTY><PROPERTY Name="cmiPnPDevClassGUID" Format="String">{72631e54-78a4-11d0-bcf7-00aa00b7b32a}</PROPERTY><PROPERTY Name="cmiIsCriticalDevice" Format="Boolean">0</PROPERTY><DISPLAYNAME>Microsoft APM Legacy Battery</DISPLAYNAME><VERSION>5.1.2517.0</VERSION><DESCRIPTION>Microsoft APM Legacy Battery</DESCRIPTION><VENDOR>Microsoft</VENDOR><OWNERS>matth</OWNERS><AUTHORS>matth</AUTHORS><DATECREATED>7/16/2001</DATECREATED><RESOURCE ResTypeVSGUID="{AFC59066-28EA-4279-979B-955C9E8DE82A}" BuildTypeMask="819" Name="PNPID(819):&quot;NTAPM\APMBATT&quot;"><PROPERTY Name="PnPID" Format="String">NTAPM\APMBATT</PROPERTY><PROPERTY Name="cmiIsCompatibleID" Format="Boolean">0</PROPERTY><PROPERTY Name="ServiceName" Format="String">apmbatt</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{E66B49F6-4A35-4246-87E8-5C1A468315B5}" BuildTypeMask="819" Name="File:&quot;%17%&quot;,&quot;battery.inf&quot;"><PROPERTY Name="DstPath" Format="String">%17%</PROPERTY><PROPERTY Name="DstName" Format="String">battery.inf</PROPERTY><PROPERTY Name="NoExpand" Format="Boolean">0</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{E66B49F6-4A35-4246-87E8-5C1A468315B5}" BuildTypeMask="819" Name="File:&quot;%12%&quot;,&quot;apmbatt.sys&quot;"><PROPERTY Name="DstPath" Format="String">%12%</PROPERTY><PROPERTY Name="DstName" Format="String">apmbatt.sys</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{E66B49F6-4A35-4246-87E8-5C1A468315B5}" BuildTypeMask="819" Name="File:&quot;%12%&quot;,&quot;battc.sys&quot;"><PROPERTY Name="DstPath" Format="String">%12%</PROPERTY><PROPERTY Name="DstName" Format="String">battc.sys</PROPERTY></RESOURCE><RESOURCE ResTypeVSGUID="{5C16ED57-3182-4411-8EA7-AC1CE70B96DA}" BuildTypeMask="819" Name="Service(819):&quot;apmbatt&quot;"><PROPERTY Name="ServiceName" Format="String">apmbatt</PROPERTY><PROPERTY Name="ServiceDisplayName" Format="String">Microsoft APM Legacy Battery Driver</PROPERTY><PROPERTY Name="ServiceType" Format="Integer">1</PROPERTY><PROPERTY Name="StartType" Format="Integer">3</PROPERTY><PROPERTY Name="ErrorControl" Format="Integer">1</PROPERTY><PROPERTY Name="ServiceBinary" Format="String">%12%\apmbatt.sys</PROPERTY></RESOURCE><GROUPMEMBER GroupVSGUID="{DE577689-9566-11D4-8E84-00B0D03D27C6}"/><DEPENDENCY Class="Include" Type="All" DependOnGUID="{7573FB43-D959-47b2-B7C8-0D847CF57104}"/></COMPONENT></DCARRIER>
