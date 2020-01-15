package com.evil.web;

import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;
import java.util.*;

public class ServerListVersionServlet extends HttpServlet 
{
	public void doGet(HttpServletRequest request, HttpServletResponse response) throws IOException, ServletException
	{
		//http://127.0.0.1:8080/evil/version.slist
		response.setContentType("text/html");
		ServletContext ctx = getServletContext();
		InputStream is = ctx.getResourceAsStream("/res/slist_version");
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
