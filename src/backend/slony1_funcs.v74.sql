-- ----------------------------------------------------------------------
-- slony1_funcs.v74.sql
--
--    Version 7.4 specific part of the replication support functions.
--
--	Copyright (c) 2003-2004, PostgreSQL Global Development Group
--	Author: Jan Wieck, Afilias USA INC.
--
-- $Id: slony1_funcs.v74.sql,v 1.2.2.1 2004-09-29 14:35:08 wieck Exp $
-- ----------------------------------------------------------------------

-- ----------------------------------------------------------------------
-- FUNCTION truncateTable(tab_fqname)
--
--	Remove all content from a table before the subscription
--	content is loaded via COPY.
-- ----------------------------------------------------------------------
create or replace function @NAMESPACE@.truncateTable(text)
returns int4
as '
declare
	p_tab_fqname		alias for $1;
begin
	execute ''truncate '' || p_tab_fqname;
	return 1;
end;
' language plpgsql;

