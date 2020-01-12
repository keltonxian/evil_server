package com.evil.web;

import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;
import java.util.*;

public class ServerListServlet extends HttpServlet 
{
	public void doGet(HttpServletRequest request, HttpServletResponse response) throws IOException, ServletException
	{
		//http://127.0.0.1:8080/evil/patch.slist?version=1&flag=0
		String version = request.getParameter("version");
		String flag = request.getParameter("flag");
		//response.setContentType("text/html");
		response.setContentType("application/zip");
		//PrintWriter out = response.getWriter();
		//out.println("<br>Got version " + version);

		ServletContext ctx = getServletContext();
		//InputStream is = ctx.getResourceAsStream("/res/slist.txt");
		InputStream is = ctx.getResourceAsStream("/res/slist.zip");

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
