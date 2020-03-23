import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.net.http.WebSocket.Listener;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Set;
import java.util.StringTokenizer;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;

import purejavacomm.CommPort;
import purejavacomm.CommPortIdentifier;
import purejavacomm.SerialPort;

/**
 * @author costing
 * @since Mar 17, 2020
 */
public class JMRIFaders {
	private static long lastMessage = System.currentTimeMillis();

	private static String serverAddress;

	private static Throttle a;

	private static Throttle b;

	private static final ConcurrentHashMap<String, String> serialQueue = new ConcurrentHashMap<>();

	private static final Object lock = new Object();

	private static void sendToJMRI(final String data) {
		if (webSocketInstance != null) {
			final String message = "{\"type\": \"throttle\", \"data\": {" + data + "}}";

			System.err.println("-> JMRI : " + message);

			webSocketInstance.sendText(message, true);

			lastMessage = System.currentTimeMillis();
		}
	}

	/**
	 * @author costing
	 * @since Mar 19, 2020
	 */
	public static class Throttle {
		private final int address;
		private final String name;

		private float speed = 0;
		private boolean forward = true;

		/**
		 * @param address
		 * @param name
		 */
		public Throttle(final int address, final String name) {
			this.address = address;
			this.name = name;

			System.err.println("Throttle " + this.name + ": " + this.address);
		}

		/**
		 * @param position
		 */
		public void fromFader(final int position) {
			if (position < 127) {
				speed = (126 - position) / 126.f;
				sendToJMRI(toString() + ", \"forward\": false, \"speed\": " + speed);
			}
			else
				if (position > 128) {
					speed = (position - 129) / 126.f;
					sendToJMRI(toString() + ", \"forward\": true, \"speed\": " + speed);
				}
				else {
					sendToJMRI(toString() + ", \"speed\": 0");
				}
		}

		/**
		 * @param speed
		 *            new speed
		 */
		public void setSpeed(final float speed) {
			if (Math.abs(this.speed - speed) > 0.0001) {
				this.speed = speed;
				apply();
			}
		}

		/**
		 * @param forward
		 *            new direction
		 */
		public void setForward(final boolean forward) {
			if (this.forward != forward) {
				this.forward = forward;
				apply();
			}
		}

		/**
		 * A message received from JMRI
		 */
		public void apply() {
			int toFader = Math.round(speed * 127);

			if (toFader <= 0) {
				// direction doesn't matter, the loco is stopped, middle position is 128
				toFader = 128;
			}
			else
				if (forward)
					toFader += 128;
				else
					toFader = 127 - toFader;

			serialQueue.put(name, toFader + name);

			synchronized (lock) {
				lock.notifyAll();
			}
		}

		@Override
		public String toString() {
			return "\"address\": " + address + ", \"name\": \"" + name + "\"";
		}

		/**
		 *
		 */
		public void init() {
			sendToJMRI(toString());
		}
	}

	private static WebSocket webSocketInstance = null;

	private static Listener wsListener = new Listener() {
		@Override
		public CompletionStage<?> onText(final WebSocket webSocket, final CharSequence data, final boolean last) {
			final StringTokenizer st = new StringTokenizer(data.toString(), "{},");

			boolean fora = false;
			boolean forb = false;
			float speed = -1;
			boolean hasSpeed = false;
			boolean forward = false;
			boolean hasForward = false;

			// quick and dirty parser for the JSON lines sent from JMRI
			while (st.hasMoreTokens()) {
				final String tok = st.nextToken();

				switch (tok) {
				case "\"name\":\"A\"":
				case "\"throttle\":\"A\"":
					fora = true;
					break;

				case "\"name\":\"B\"":
				case "\"throttle\":\"B\"":
					forb = true;
					break;

				case "\"forward\":true":
					forward = true;
					hasForward = true;
					break;

				case "\"forward\":false":
					forward = false;
					hasForward = true;
					break;

				default:
					if (tok.startsWith("\"speed\":")) {
						final String newSpeed = tok.substring("\"speed\":".length());
						speed = Float.parseFloat(newSpeed);
						System.err.println("New speed: " + speed + " from " + speed);

						hasSpeed = true;
					}
				}
			}

			if (hasSpeed) {
				if (fora)
					a.setSpeed(speed);
				if (forb)
					b.setSpeed(speed);
			}

			if (hasForward) {
				if (fora)
					a.setForward(forward);
				if (forb)
					b.setForward(forward);
			}

			return Listener.super.onText(webSocket, data, last);
		}

		@Override
		public void onOpen(final WebSocket webSocket) {
			Listener.super.onOpen(webSocket);
		}

		@Override
		public CompletionStage<?> onClose(final WebSocket webSocket, final int statusCode, final String reason) {
			webSocketInstance = null;

			return Listener.super.onClose(webSocket, statusCode, reason);
		}
	};

	private static BufferedReader br = null;

	private static class SerialReader extends Thread {
		@Override
		public void run() {
			int sleep = 100;

			while (true) {
				if (br != null) {
					String line = null;

					try {
						line = br.readLine();

						if (line.length() > 0) {
							int idx = line.indexOf('{');

							while (idx >= 0) {
								final int idx2 = line.indexOf('}', idx + 1);

								if (idx2 > idx) {
									final String content = line.substring(idx + 1, idx2).trim();
									final int column = content.indexOf(':');

									if (column > 0) {
										final String name = content.substring(0, column);
										final String value = content.substring(column + 1);

										try {
											final int iValue = Integer.parseInt(value);

											if (name.equals("A"))
												a.fromFader(iValue);
											else
												if (name.equals("B"))
													b.fromFader(iValue);
										}
										catch (@SuppressWarnings("unused") final NumberFormatException nfe) {
											System.err.println("Invalid string: " + content);
										}
									}

									idx = line.indexOf('{', idx2 + 1);
								}
								else
									break;
							}
						}
					}
					catch (@SuppressWarnings("unused") final IOException ioe) {
						// ignore
					}

					if (line == null) {
						br = null;
					}
				}
				else {
					// wait to be connected to the serial port
					try {
						Thread.sleep(sleep);
					}
					catch (@SuppressWarnings("unused") final InterruptedException e) {
						return;
					}

					if (sleep < 2000)
						sleep += 100;
				}
			}
		}
	}

	private static SerialReader serialReader = new SerialReader();

	private static SerialPort activeSerialPort = null;

	@SuppressWarnings("resource")
	private static boolean sendToFader(final String data) {
		if (activeSerialPort != null) {
			try {
				final OutputStream os = activeSerialPort.getOutputStream();

				for (final char c : data.toCharArray()) {
					os.write(c);
				}

				return true;
			}
			catch (@SuppressWarnings("unused") final IOException e) {
				activeSerialPort = null;
			}
		}

		return false;
	}

	private static boolean connectToJMRI() {
		System.err.println("Reconnecting to JMRI");

		final var client = HttpClient.newHttpClient();

		try {
			webSocketInstance = client.newWebSocketBuilder().buildAsync(URI.create("ws://" + serverAddress + "/json/"), wsListener).join();

			a.init();
			b.init();

			return true;
		}
		catch (@SuppressWarnings("unused") final Throwable t) {
			System.err.println("  cannot connect");
		}

		return false;
	}

	private static Set<String> triedSerialPorts = new HashSet<>();

	@SuppressWarnings("resource")
	private static synchronized boolean connectToSerial() {
		final Enumeration<CommPortIdentifier> enumeration = CommPortIdentifier.getPortIdentifiers();

		final Set<String> newTriedPorts = new HashSet<>();

		while (enumeration.hasMoreElements() && activeSerialPort == null) {
			final CommPortIdentifier cpi = enumeration.nextElement();

			final String comPortName = cpi.getName();

			newTriedPorts.add(comPortName);

			if (triedSerialPorts.contains(comPortName)) {
				// I've seen these ports before and they didn't work
				continue;
			}

			try {
				final CommPort cp = cpi.open("JMRIFaders", 100);

				if (cp instanceof SerialPort) {
					System.err.println("Trying to talk to " + cpi.getName());

					activeSerialPort = (SerialPort) cp;

					activeSerialPort.setSerialPortParams(115200, SerialPort.DATABITS_8, SerialPort.STOPBITS_1, SerialPort.PARITY_NONE);

					Thread.sleep(3000);
					// let Arduino boot

					activeSerialPort.getOutputStream().write('v');

					activeSerialPort.enableReceiveTimeout(100);

					final InputStream is = activeSerialPort.getInputStream();

					final int i = is.read();

					if (i == '{') {
						// ok
						br = new BufferedReader(new InputStreamReader(is));

						final String version = br.readLine();

						activeSerialPort.disableReceiveTimeout();

						if (version.equals("Fader:v1}")) {
							System.err.println("  This is the correct endpoint");
						}
						else {
							System.err.println("  Unknown device, signature = " + version);
							activeSerialPort = null;
						}
					}
					else {
						System.err.println("  Unknown device");

						activeSerialPort = null;
					}
				}
			}
			catch (@SuppressWarnings("unused") final Throwable t) {
				activeSerialPort = null;
				// t.printStackTrace();

				// System.err.println(" Fail to open: " + t.getMessage());
			}
		}

		triedSerialPorts = newTriedPorts;

		if (activeSerialPort != null) {
			System.err.println("Connected to " + activeSerialPort.getName());
			return true;
		}

		return false;
	}

	private static Thread consumerThread = new Thread() {
		@Override
		public void run() {
			while (true) {
				String message = serialQueue.remove("A");

				if (message == null)
					message = serialQueue.remove("B");

				if (message != null) {
					sendToFader(message);
					try {
						// CTS doesn't seem to be available, make sure messages are spaced out in time
						sleep(10);
					}
					catch (@SuppressWarnings("unused") final InterruptedException e) {
						return;
					}
				}

				synchronized (lock) {
					try {
						lock.wait(100);
					}
					catch (@SuppressWarnings("unused") final InterruptedException e) {
						return;
					}
				}
			}
		}
	};

	/**
	 * @param args
	 * @throws InterruptedException
	 */
	public static void main(final String[] args) throws InterruptedException {
		int aAddress = 50;
		int bAddress = 60;
		String host = "localhost";
		int port = 12080;

		// parse command line arguments
		for (int i = 0; i < args.length - 1; i++) {
			switch (args[i]) {
			case "-h":
				host = args[++i];
				break;
			case "-p":
				port = Integer.parseInt(args[++i]);
				break;
			case "-a":
				aAddress = Integer.parseInt(args[++i]);
				break;
			case "-b":
				bAddress = Integer.parseInt(args[++i]);
				break;
			default:
				System.err.println("Unknown command line parameter: " + args[i]);
				return;
			}
		}

		// now initialize the structures
		serverAddress = host + ":" + port;
		a = new Throttle(aAddress, "A");
		b = new Throttle(bAddress, "B");

		System.err.println("JMRI web server address: " + serverAddress);

		serialReader.start();
		consumerThread.start();

		while (true) {
			try {
				if (activeSerialPort == null) {
					if (!connectToSerial()) {
						Thread.sleep(2000);
						continue;
					}

					// after reconnecting to the faders, apply the known position
					a.apply();
					b.apply();
				}

				if (webSocketInstance == null) {
					connectToJMRI();
				}
				else {
					// ping
					if (System.currentTimeMillis() - lastMessage > 5000)
						a.init();
				}
			}
			catch (final Throwable t) {
				System.err.println(t.getMessage());
			}

			Thread.sleep(1000);
		}
	}
}
