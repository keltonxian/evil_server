package com.evil.common;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

public class ZipUtil {

    private List<String> fileList;

    public ZipUtil() {
        fileList = new ArrayList<String> ();
    }

    public void doZip(String sourcePath, String zipFile) {
        generateFileList(new File(sourcePath), sourcePath);
        byte[] buffer = new byte[1024];
        String source = new File(sourcePath).getName();
        FileOutputStream fos = null;
        ZipOutputStream zos = null;
        try {
            fos = new FileOutputStream(zipFile);
            zos = new ZipOutputStream(fos);

            System.out.println("Output to Zip : " + zipFile);
            FileInputStream in = null;

            for (String file: this.fileList) {
                System.out.println("File Added : " + file);
                ZipEntry ze = new ZipEntry(source + File.separator + file);
                zos.putNextEntry(ze);
                try {
                    in = new FileInputStream(sourcePath + File.separator + file);
                    int len;
                    while ((len = in .read(buffer)) > 0) {
                        zos.write(buffer, 0, len);
                    }
                } finally {
                    in.close();
                }
            }

            zos.closeEntry();
            System.out.println("Folder successfully compressed");

        } catch (IOException ex) {
            ex.printStackTrace();
        } finally {
            try {
                zos.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    public void generateFileList(File node, String sourcePath) {
        // add file only
        if (node.isFile()) {
            fileList.add(generateZipEntry(node.toString(), sourcePath));
        }

        if (node.isDirectory()) {
            String[] subNote = node.list();
            for (String filename: subNote) {
                generateFileList(new File(node, filename), sourcePath);
            }
        }
    }

    private String generateZipEntry(String file, String sourcePath) {
        return file.substring(sourcePath.length() + 1, file.length());
    }

	public void doUnzip(String zipFile, String destinationFolder) {
		File directory = new File(destinationFolder);
        
		// if the output directory doesn't exist, create it
		if(!directory.exists()) 
			directory.mkdirs();

		// buffer for read and write data to file
		byte[] buffer = new byte[2048];
        
		try {
			FileInputStream fInput = new FileInputStream(zipFile);
			ZipInputStream zipInput = new ZipInputStream(fInput);
            
			ZipEntry entry = zipInput.getNextEntry();
            
			while(entry != null){
				String entryName = entry.getName();
				File file = new File(destinationFolder + File.separator + entryName);
                
				System.out.println("Unzip file " + entryName + " to " + file.getAbsolutePath());
                
				// create the directories of the zip directory
				if(entry.isDirectory()) {
					File newDir = new File(file.getAbsolutePath());
					if(!newDir.exists()) {
						boolean success = newDir.mkdirs();
						if(success == false) {
							System.out.println("Problem creating Folder");
						}
					}
                }
				else {
					FileOutputStream fOutput = new FileOutputStream(file);
					int count = 0;
					while ((count = zipInput.read(buffer)) > 0) {
						// write 'count' bytes to the file output stream
						fOutput.write(buffer, 0, count);
					}
					fOutput.close();
				}
				// close ZipEntry and take the next one
				zipInput.closeEntry();
				entry = zipInput.getNextEntry();
			}
            
			// close the last ZipEntry
			zipInput.closeEntry();
            
			zipInput.close();
			fInput.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
    }

}
