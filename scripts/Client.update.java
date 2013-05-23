import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import java.util.List;
import java.util.ArrayList;
import java.util.Collections;

import hyperclient.HyperClient;
import hyperclient.Range;
import hyperclient.SearchBase;

public class Search {
   
    static private String COORD_IP = "172.31.0.33";
    static private int MAX_THR = 32;
    static private int SECS    = 60;
    static private double NANO = 1000000000.0;
    static int SUM = 0;
    static Random rand = new Random();

    static String[] regions = {"10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", 
        "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", 
        "46", "47", "48", "49", "50", ".7", "90", "C3", "g6", "H9", "L1", "l2", "n4", "O5", "q8"};
    
    static int[] sizes = {270, 2786, 1332, 434, 2834, 1926, 3135, 7537, 1496, 2454, 1024, 1251, 2893, 1785, 3479, 1334, 2330, 2423, 605, 398, 788, 2735, 
        2079, 1170, 801, 1491, 1324, 1310, 7565, 2009, 481, 601, 772, 1932, 1007, 1014, 9776, 451, 1772, 2269, 647, 4262, 1955, 984, 2034, 247, 419, 
        628, 556, 792, 3125};

    static int[] stars = {53067, 2532, 21160, 16667, 4960, 336};

    static int getIndex(String reg) {
        for (int i = 0; i < regions.length; i++)
            if (regions[i] == reg) return i;
        return -1;
    }
    
    private static String shuffleStr(String word) {  
        List<Character> characters = new ArrayList<Character>();  
        for(char c : word.toCharArray()) {  
            characters.add(c);
        }  
        Collections.shuffle(characters);  
        StringBuilder sb = new StringBuilder();  
        for(char c : characters) {  
            sb.append(c);  
        }  
        return sb.toString();  
    }  

    public static class Client extends Thread {
        private int id;
        public volatile int cnt;
        private long duration;
        
        public Client(int _id, int _duration) {
            this.id = _id;
            this.duration = _duration;
            this.cnt = 0;
        }
        
        public void run() {
            
            HashMap<String, Object> values = new HashMap<String, Object>();
            HyperClient m_client = new HyperClient(COORD_IP, 1982);

            try {
                long end, begin = System.nanoTime(), lb, le;
                hello:while (true) {
                    //String region = regions[rand.nextInt(regions.length)];
                    String key = Integer.toString(rand.nextInt(2550));
                    Map row = m_client.get("hotels", key);
                    int error = 0;
                    while (true) {
                        try {
                            lb = System.nanoTime();
                            values.put("region", row.get("region"));
                            values.put("name", row.get("name"));
                            values.put("category", row.get("category"));
                            values.put("lowest_price", row.get("lowest_price"));
                            values.put("highest_price", row.get("highest_price"));
                            values.put("ratings", row.get("ratings"));
                            values.put("status", row.get("status"));
                            values.put("stars", row.get("stars"));
                            //values.put("tel", row.get("tel"));
                            values.put("tel", shuffleStr(row.get("tel").toString()));
                            values.put("locality", row.get("locality"));
                            values.put("postcode", row.get("postcode"));
                            values.put("longitude", row.get("longitude"));
                            values.put("latitude", row.get("latitude"));
                            values.put("address", row.get("address"));
                            m_client.put("hotels", key, values);
                            //s = m_client.search("hotels", values);
                            le = System.nanoTime();
                            long ls = le - lb;
                            int count=0;
                            lb = System.nanoTime();
                            //while (s.hasNext()) {
                            //    s.next();
                            //    count++;
                            //    end = System.nanoTime();
                            //    if ((end - begin) / NANO > duration) {
                            //        System.out.println("Search Latency = " + ls + " DID_NOT_FINISH Counter = "+(cnt+1)+" ID = "+id+" Error = "+error);
                            //        break hello; 
                            //    }
                            //}
                            cnt++;
                            le = System.nanoTime();
                            System.out.println("Search Latency = " + ls + " Next Latency = "+(le - lb)+" Counter = "+cnt+" ID = "+id+" Error = "+error);
                            //String str = (sizes[getIndex(region)] == count) ? "CORRECT" : "WRONG!!";
                            //System.err.println(count+" "+str);
                            end = System.nanoTime();
                            if ((end - begin) / NANO > duration)
                                break hello; 
                        }
                        catch (Exception e) {
                            error++;
                            end = System.nanoTime();
                            if ((end - begin) / NANO > duration) {
                                System.out.println("Couldn finish search, tried = "+error+" "+e);
                                break hello;
                            }
                        }
                    }
                }
                //synchronized(Thread.class) {
                //    Search.SUM += cnt;
                //}
            }
            catch (Throwable t) {
                throw new RuntimeException(t);
            }
            
        }
    }
    
    public static void main(String[] args) {
        
        Client[] clients = new Client[MAX_THR];
        for (int i = 0; i < clients.length; i++) {
            clients[i] = new Client(i, SECS);
        }
        
        System.out.println("STARTING");
        long startTime = System.nanoTime();
        
        try {
            for (int i = 0; i < clients.length; i++)
                clients[i].start();

            for (int i = 0; i < clients.length; i++) {
                clients[i].join((long)(SECS * 1000 * 1.1));
                if (clients[i].isAlive()) 
                    break;
            }
        } catch (Throwable t) {
                throw new RuntimeException(t);
        }
        for (int i = 0; i < clients.length; i++) {
            Search.SUM += clients[i].cnt;
        }

        long endTime = System.nanoTime();
        double secs = (endTime - startTime)/1000000000.0;
        System.out.println("DONE");        
        System.out.println(SECS + " secs, " + ((double)SUM) / SECS + " ops/sec"); 
        
        System.exit(0);
    }
}
