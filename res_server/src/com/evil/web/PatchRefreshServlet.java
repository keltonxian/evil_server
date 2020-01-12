package com.evil.web;

import com.evil.common.ZipUtil;
import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;
import java.util.*;

public class PatchRefreshServlet extends HttpServlet 
{

	public void doGet(HttpServletRequest request, HttpServletResponse response) throws IOException, ServletException
	{
		ServletContext ctx = getServletContext();
		ZipUtil appZip = new ZipUtil();
		appZip.doUnzip(ctx.getRealPath("res/lua/g.zip"), ctx.getRealPath("res"));
		appZip.doUnzip(ctx.getRealPath("res/lua/l.zip"), ctx.getRealPath("res"));
		appZip.doZip(ctx.getRealPath("res/core"), ctx.getRealPath("res/core.zip"));
		/**
		response.setContentType("text/html");
		PrintWriter out = response.getWriter();
		out.println("<br>Save in " + path2);
		*/
	}

}
