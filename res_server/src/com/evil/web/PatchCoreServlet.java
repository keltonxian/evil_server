package com.evil.web;

import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;
import java.util.*;

public class PatchCoreServlet extends HttpServlet 
{
	public void doGet(HttpServletRequest request, HttpServletResponse response) throws IOException, ServletException
	{
		//http://127.0.0.1:8080/evil/patch.core?game_ver=0001&logic_ver=0001
		String gameVer = request.getParameter("game_ver");
		String logicVer = request.getParameter("logic_ver");
		response.setContentType("application/zip");
		//PrintWriter out = response.getWriter();
		//out.println("<br>Got version " + version);

		ServletContext ctx = getServletContext();
		InputStream is = ctx.getResourceAsStream("/res/core.zip");

		int read = 0;
		byte[] bytes = new byte[1024];

		OutputStream os = response.getOutputStream();
		while ((read = is.read(bytes)) != -1)
		{
			os.write(bytes, 0, read);
		}
		os.flush();
		os.close();
	}
}
